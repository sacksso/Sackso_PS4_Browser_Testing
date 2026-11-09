// Motor de descargas por trozos (ver dlengine.h).
#include "dlengine.h"

#include <sys/stat.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

#include "asyncwriter.h"

namespace {
typedef std::chrono::steady_clock Clock;
long long nowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count(); }
void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
thread_local int tlRank = 0;   // posicion del trabajador (0 = el primero): los de rango >= 'allowed' se aparcan
const size_t kFlushBuf = 2u * 1024 * 1024;  // cada trabajador junta 2 MB antes de encolar: menos saltos de cabeza en el HDD con varias conexiones

struct Meta {
    std::string url, finalPath;
    long long total = 0, chunk = 0;
    std::vector<int> done;
};
bool fileSize(const std::string& p, long long& sz) {
    struct stat st;
    if (stat(p.c_str(), &st) != 0) return false;
    sz = static_cast<long long>(st.st_size);
    return true;
}
bool writeMeta(const std::string& path, const Meta& m) {
    const std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "w");
    if (!f) return false;
    fprintf(f, "v=1\nurl=%s\nfinal=%s\ntotal=%lld\nchunk=%lld\ndone=", m.url.c_str(), m.finalPath.c_str(), m.total, m.chunk);
    for (size_t i = 0; i < m.done.size(); i++) fprintf(f, "%s%d", i ? "," : "", m.done[i]);
    fprintf(f, "\n");
    const bool ok = fclose(f) == 0;
    if (!ok) { remove(tmp.c_str()); return false; }
    remove(path.c_str());
    return rename(tmp.c_str(), path.c_str()) == 0;
}
bool readMeta(const std::string& path, Meta& m) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return false;
    std::string all;
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) all.append(buf, n);
    fclose(f);
    m = Meta();
    bool v1 = false;
    std::string doneList;
    size_t p = 0;
    while (p < all.size()) {
        size_t e = all.find('\n', p);
        if (e == std::string::npos) e = all.size();
        std::string s = all.substr(p, e - p);
        p = e + 1;
        while (!s.empty() && s.back() == '\r') s.pop_back();
        const size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = s.substr(0, eq), v = s.substr(eq + 1);
        if (k == "v") v1 = v == "1";
        else if (k == "url") m.url = v;
        else if (k == "final") m.finalPath = v;
        else if (k == "total") m.total = atoll(v.c_str());
        else if (k == "chunk") m.chunk = atoll(v.c_str());
        else if (k == "done") doneList = v;
    }
    size_t i = 0;
    while (i < doneList.size()) {
        size_t e = doneList.find(',', i);
        if (e == std::string::npos) e = doneList.size();
        if (e > i) m.done.push_back(atoi(doneList.substr(i, e - i).c_str()));
        i = e + 1;
    }
    return v1 && !m.url.empty() && !m.finalPath.empty() && m.total > 0 && m.chunk > 0;
}
// Indice de descargas incompletas (<carpeta>/.partials, una ruta de .meta por linea). Evita opendir/readdir, que este proyecto no usa en ningun otro sitio.
std::string indexPathFor(const std::string& metaPath) {
    const size_t sl = metaPath.find_last_of('/');
    return (sl == std::string::npos ? std::string(".") : metaPath.substr(0, sl)) + "/.partials";
}
std::vector<std::string> indexRead(const std::string& idx) {
    std::vector<std::string> v;
    FILE* f = fopen(idx.c_str(), "r");
    if (!f) return v;
    std::string all;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) all.append(buf, n);
    fclose(f);
    size_t p = 0;
    while (p < all.size()) {
        size_t e = all.find('\n', p);
        if (e == std::string::npos) e = all.size();
        std::string l = all.substr(p, e - p);
        p = e + 1;
        while (!l.empty() && l.back() == '\r') l.pop_back();
        if (!l.empty()) v.push_back(l);
    }
    return v;
}
void indexWrite(const std::string& idx, const std::vector<std::string>& v) {
    FILE* f = fopen(idx.c_str(), "w");
    if (!f) return;
    for (const std::string& l : v) fprintf(f, "%s\n", l.c_str());
    fclose(f);
}
void indexAdd(const std::string& metaPath) {
    const std::string idx = indexPathFor(metaPath);
    std::vector<std::string> v = indexRead(idx);
    for (const std::string& l : v) if (l == metaPath) return;
    v.push_back(metaPath);
    indexWrite(idx, v);
}
long long metaDoneBytes(const Meta& m) {
    long long b = 0;
    const long long n = (m.total + m.chunk - 1) / m.chunk;
    for (int i : m.done) if (i >= 0 && i < n) b += std::min(m.chunk, m.total - static_cast<long long>(i) * m.chunk);
    return b;
}
}  // namespace

// ------------------------------------------------------------------ Impl
struct DlEngine::Impl {
    enum Mode { M_UNKNOWN, M_CHUNKED, M_OPEN, M_STREAM200 };
    DlParams P;
    AsyncWriter w;
    std::mutex mu;
    std::vector<uint8_t> st;  // 0 pendiente, 1 en curso, 2 completo
    long long total = -1, chunk = 0;
    int nChunks = 0, nextIdx = 0;
    Mode mode = M_UNKNOWN;
    std::atomic<bool> headersReady{false};
    bool writerOpen = false, completedOpen = false, firstClaimed = false;
    std::atomic<bool> restartScratch{false};
    bool resumeCand = false, alreadyDone = false;
    Meta rm;
    std::string curUrl, finalUrl, finalPath, partPath, metaPath, name, contentType;
    std::atomic<long long> done{0};
    std::atomic<bool> cancel{false}, fatal{false}, w0done{false}, fin{false};
    std::atomic<int> active{0}, retryCount{0};
    std::atomic<int> allowed{1}, alive{0}, ceiling{1}, inflight{0};   // conexiones permitidas ahora / hilos vivos / techo (baja ante errores)
    std::atomic<long long> lastDownAt{-100000};
    std::atomic<bool> rampStopped{false};
    DlResult fatalResult = DlResult::NET_ERROR;
    std::string fstage;
    int fstatus = 0, fsce = 0;
    bool resumed = false;
    long long resumedFrom = 0;
    int connsUsed = 1;
    long long t0 = 0;
    std::vector<std::thread> workers;
    std::thread coord;
    DlOutcome out;
    mutable std::mutex outMu;

    void log(const std::string& s) { if (P.log) P.log(s); }
    bool deadlinePassed() const { return P.discard && nowMs() - t0 >= P.benchMs; }
    void setFatal(DlResult r, const std::string& stage, int status, int sce) {  // con 'mu' tomado o no: solo escribe campos simples
        if (fatal.load()) return;
        fatalResult = r; fstage = stage; fstatus = status; fsce = sce;
        fatal = true;
    }
    void saveMetaLocked() {
        if (P.discard || mode != M_CHUNKED || metaPath.empty()) return;
        Meta m;
        m.url = P.url; m.finalPath = finalPath; m.total = total; m.chunk = chunk;
        for (size_t i = 0; i < st.size(); i++) if (st[i] == 2) m.done.push_back(static_cast<int>(i));
        if (writeMeta(metaPath, m)) indexAdd(metaPath);
    }

    struct Sink : DlChunkSink {
        Impl* E;
        long long start, next;
        std::string buf;
        uint64_t lastSeq = 0;
        long long got = 0;       // bytes entregados a la cola de disco (o contados en modo medidor)
        bool byDeadline = false, headers = false;
        DlFetchResult hdr;
        Sink(Impl* e, long long s) : E(e), start(s), next(s) {}
        bool begin(const DlFetchResult& r) override { headers = true; hdr = r; return E->onHeaders(r, start); }
        bool write(const void* d, size_t n) override {
            if (E->cancel.load() || E->fatal.load() || E->restartScratch.load()) return false;
            if (E->P.discard) {
                E->done += static_cast<long long>(n);
                got += static_cast<long long>(n);
                if (E->deadlinePassed()) { byDeadline = true; return false; }
                return true;
            }
            buf.append(static_cast<const char*>(d), n);
            return buf.size() < kFlushBuf || flush();
        }
        bool flush() {
            if (buf.empty() || E->P.discard) return true;
            uint64_t sq = 0;
            if (!E->w.writeAt(next, buf.data(), buf.size(), &sq)) { E->setFatal(DlResult::DISK_ERROR, "disco", 0, 0); return false; }
            lastSeq = sq;
            next += static_cast<long long>(buf.size());
            got += static_cast<long long>(buf.size());
            E->done += static_cast<long long>(buf.size());
            buf.clear();
            return true;
        }
    };

    // Cabeceras de una respuesta. false = abortar esta peticion (con 'fatal' o 'restartScratch' si procede).
    bool onHeaders(const DlFetchResult& r, long long start) {
        std::lock_guard<std::mutex> lk(mu);
        if ((r.status == 429 || r.status == 503) && !P.discard) return false;  // limite temporal del servidor: lo reintenta runChunk (con espera y menos conexiones)
        if (r.status != 206 && r.status != 200) { setFatal(DlResult::BAD_STATUS, "", r.status, 0); fstatus = r.status; return false; }
        if (mode != M_UNKNOWN) {  // respuestas posteriores: deben seguir siendo coherentes
            if (mode == M_CHUNKED && r.status != 206) return false;                                      // dejo de respetar Range: se reintenta
            if (mode == M_CHUNKED && r.rangeTotal > 0 && r.rangeTotal != total) { setFatal(DlResult::NET_ERROR, "el archivo cambio en el servidor", r.status, 0); return false; }
            return true;
        }
        if (P.accept && !P.accept(r)) { setFatal(DlResult::HTML_INSTEAD, "", r.status, 0); return false; }
        finalUrl = r.finalUrl.empty() ? P.url : r.finalUrl;
        curUrl = finalUrl;
        contentType = r.contentType;
        if (r.status == 200) {
            if (start > 0) { restartScratch = true; return false; }  // reanudar pero el servidor no admite Range: empezar de cero
            mode = M_STREAM200;
            total = r.contentLength >= 0 ? r.contentLength : -1;
        } else {
            const long long tot = r.rangeTotal > 0 ? r.rangeTotal : P.knownTotal;
            if (tot > 0) { mode = M_CHUNKED; total = tot; } else { mode = M_OPEN; total = -1; }
            if (resumeCand && (mode != M_CHUNKED || total != rm.total || chunk != rm.chunk)) { restartScratch = true; return false; }  // cambio el archivo
        }
        if (!P.discard) {
            if (resumeCand && mode == M_CHUNKED) {
                finalPath = rm.finalPath;
                partPath = finalPath + ".part";
                metaPath = partPath + ".meta";
                if (!w.openRW(partPath, false, 0)) { restartScratch = true; return false; }
            } else {
                finalPath = P.pickPath ? P.pickPath(r) : (P.dir + "/" + (P.name.empty() ? std::string("archivo") : P.name));
                partPath = finalPath + ".part";
                metaPath = partPath + ".meta";
                if (!w.openRW(partPath, true, mode == M_CHUNKED ? total : 0)) { setFatal(DlResult::DISK_ERROR, "no se pudo crear el archivo", 0, 0); return false; }
            }
            writerOpen = true;
        }
        if (mode == M_CHUNKED) {
            nChunks = static_cast<int>((total + chunk - 1) / chunk);
            if (static_cast<int>(st.size()) != nChunks) { st.assign(static_cast<size_t>(nChunks), 0); st[0] = 1; }
        }
        name = finalPath.empty() ? P.name : finalPath.substr(finalPath.find_last_of('/') + 1);
        headersReady = true;
        log("[dleng] " + std::string(mode == M_CHUNKED ? "por trozos" : (mode == M_OPEN ? "flujo abierto con Range" : "flujo unico (sin Range)")) + ", total " +
            (total >= 0 ? std::to_string(total) : std::string("?")) + " bytes" + (resumeCand ? ", reanudando" : ""));
        return true;
    }

    int claimChunk() {
        std::lock_guard<std::mutex> lk(mu);
        if (fatal.load() || cancel.load() || restartScratch.load()) return -1;
        if (mode == M_UNKNOWN) {
            if (firstClaimed) return -1;
            firstClaimed = true;
            if (resumeCand) {  // primer trozo pendiente
                for (int i = 0; i < nChunks; i++) if (st[static_cast<size_t>(i)] == 0) { st[static_cast<size_t>(i)] = 1; nextIdx = i + 1; return i; }
                return -1;
            }
            nextIdx = 1;
            return 0;
        }
        if (mode != M_CHUNKED) return -1;
        if (P.discard) {
            if (deadlinePassed()) return -1;
            const int i = nextIdx % nChunks;
            nextIdx++;
            return i;
        }
        for (; nextIdx < nChunks; nextIdx++)
            if (st[static_cast<size_t>(nextIdx)] == 0) { st[static_cast<size_t>(nextIdx)] = 1; return nextIdx++; }
        return -1;
    }

    // El servidor rechazo una peticion que empezo cuando ya habia 'par' en curso (incluida ella): el limite es como mucho par-1.
    // Se usa min(), asi varios fallos a la vez no encadenan bajadas de mas.
    void throttleDown(const std::string& why, int par) {
        if (P.discard || P.conns <= 1) return;
        const int target = std::max(1, par - 1);
        rampStopped = true;
        const int a = allowed.load();
        if (target < a) {
            allowed = target;
            if (target < ceiling.load()) ceiling = target;
            lastDownAt = nowMs();
            log("[dleng] el servidor limita las conexiones (" + why + "): " + std::to_string(a) + " -> " + std::to_string(target));
        }
    }
    bool anyPending() {
        std::lock_guard<std::mutex> lk(mu);
        if (mode != M_CHUNKED) return false;
        for (uint8_t v : st) if (v == 0) return true;
        return false;
    }
    // Descarga el trozo 'idx' con reintentos. 1 = completo (o fin por tiempo en el medidor), 0 = fallo/cancelado, 2 = trozo devuelto a la cola (trabajador aparcado).
    int runChunk(int idx) {
        const long long cstart = static_cast<long long>(idx) * chunk;
        long long got = 0;
        int attempts = 0;
        uint64_t lastSeq = 0;
        std::string url;
        { std::lock_guard<std::mutex> lk(mu); url = curUrl.empty() ? P.url : curUrl; }
        for (;;) {
            long long reqEnd, expectEnd;
            Mode md;
            {
                std::lock_guard<std::mutex> lk(mu);
                md = mode;
                if (md == M_CHUNKED) { reqEnd = std::min(total - 1, cstart + chunk - 1); expectEnd = reqEnd; }
                else if (md == M_UNKNOWN) {
                    reqEnd = cstart + chunk - 1;
                    if (P.knownTotal > 0) reqEnd = std::min(reqEnd, P.knownTotal - 1);
                    expectEnd = reqEnd;
                } else { reqEnd = -1; expectEnd = -1; }
            }
            const long long reqStart = md == M_STREAM200 ? 0 : cstart + got;
            Sink s(this, reqStart);
            DlFetchResult r;
            const int par = ++inflight;   // peticiones en curso al empezar esta (incluida)
            const bool ok = P.fetch(url, reqStart, md == M_STREAM200 ? -1 : reqEnd, s, r);
            --inflight;
            s.flush();
            lastSeq = std::max(lastSeq, s.lastSeq);
            got = md == M_STREAM200 ? s.next : (s.next - cstart);
            if (P.discard) { got = 0; }
            if (cancel.load() || fatal.load() || restartScratch.load()) return 0;
            if (P.discard && (s.byDeadline || deadlinePassed())) return 1;
            // ¿completo?
            bool complete = false;
            Mode m2;
            { std::lock_guard<std::mutex> lk(mu); m2 = mode; }
            if (P.discard) complete = ok && s.headers;
            else if (m2 == M_CHUNKED) {
                long long tot2;
                { std::lock_guard<std::mutex> lk(mu); tot2 = total; }
                expectEnd = std::min(tot2 - 1, cstart + chunk - 1);  // el total pudo conocerse en esta misma respuesta
                complete = expectEnd >= 0 && cstart + got > expectEnd;
            }
            else if (m2 == M_OPEN) {
                if (ok && s.headers) {
                    if (reqEnd < 0) complete = true;                      // flujo abierto hasta EOF
                    else if (cstart + got <= reqEnd) complete = true;     // el servidor termino antes de lo pedido: fin del archivo
                    else { attempts = 0; continue; }                      // primera parte entera: seguir sin limite de final
                }
            } else if (m2 == M_STREAM200) {
                std::lock_guard<std::mutex> lk(mu);
                complete = ok && s.headers && (total < 0 || got >= total);
            }
            if (complete) break;
            // fallo o respuesta corta: reintentar desde el ultimo byte recibido
            if (s.got > 0) attempts = 0;
            if (++attempts > P.retries) { setFatal(DlResult::NET_ERROR, r.stage.empty() ? "descarga cortada" : r.stage, r.status, r.sceError); return 0; }
            retryCount++;
            log("[dleng] trozo " + std::to_string(idx) + ": reintento " + std::to_string(attempts) + "/" + std::to_string(P.retries) + " desde +" + std::to_string(got) +
                " (" + (r.stage.empty() ? std::string("sin detalle") : r.stage) + ")");
            if (md == M_STREAM200) { got = 0; done = 0; }
            if (attempts >= 2) url = P.url;  // el enlace ya resuelto pudo caducar: volver a la URL original
            int wait = std::min(4000, P.backoffMs * attempts);
            const bool limited = r.status == 429 || r.status == 503 || r.stage == "sendRequest" || r.stage == "createConnection" || r.stage == "connection";
            if (limited && md == M_CHUNKED && P.conns > 1) {
                throttleDown(r.status ? "HTTP " + std::to_string(r.status) : r.stage, par);
                if (tlRank >= allowed.load()) {   // este trabajador sobra: devuelve el trozo y se aparca
                    std::lock_guard<std::mutex> lk(mu);
                    st[static_cast<size_t>(idx)] = 0;
                    if (idx < nextIdx) nextIdx = idx;
                    done -= got;
                    return 2;
                }
            }
            if (r.retryAfterMs > 0) wait = std::max(wait, std::min(r.retryAfterMs, 30000));
            for (int t = 0; t < wait && !cancel.load(); t += 50) sleepMs(50);
            if (cancel.load()) return 0;
        }
        if (P.discard) return 1;
        Mode m3;
        { std::lock_guard<std::mutex> lk(mu); m3 = mode; }
        if (m3 == M_CHUNKED && lastSeq > 0 && !w.waitWritten(lastSeq)) { setFatal(DlResult::DISK_ERROR, "disco", 0, 0); return 0; }  // sin el cerrojo: puede tardar
        std::lock_guard<std::mutex> lk(mu);
        if (m3 == M_CHUNKED) {
            st[static_cast<size_t>(idx)] = 2;
            saveMetaLocked();
        } else {
            completedOpen = true;
        }
        return 1;
    }

    void workerMain(bool first, int rank) {
        tlRank = rank;
        ++active;
        for (;;) {
            // aparcado: este trabajador sobra por ahora (el servidor limito las conexiones o la subida gradual aun no llego a el)
            while (rank >= allowed.load() && !cancel.load() && !fatal.load() && !restartScratch.load() && !P.discard) {
                if (!anyPending()) break;
                sleepMs(100);
            }
            if (rank >= allowed.load() && !P.discard && !anyPending()) break;
            const int idx = claimChunk();
            if (idx < 0) break;
            const int rc = runChunk(idx);
            if (rc == 0) break;
            { std::lock_guard<std::mutex> lk(mu); if (mode != M_CHUNKED) break; }
        }
        --active;
        --alive;
        if (first) w0done = true;
    }

    bool allChunksDone() {
        std::lock_guard<std::mutex> lk(mu);
        if (mode != M_CHUNKED) return false;
        for (uint8_t v : st) if (v != 2) return false;
        return !st.empty();
    }

    void resetForRestart() {
        if (resumeCand) { remove((rm.finalPath + ".part").c_str()); remove((rm.finalPath + ".part.meta").c_str()); }
        if (writerOpen) { w.abort(); if (!partPath.empty()) remove(partPath.c_str()); if (!metaPath.empty()) remove(metaPath.c_str()); }
        std::lock_guard<std::mutex> lk(mu);
        st.clear(); nChunks = 0; nextIdx = 0; total = -1; mode = M_UNKNOWN;
        headersReady = false; writerOpen = false; completedOpen = false; firstClaimed = false;
        resumeCand = false; resumed = false; resumedFrom = 0;
        done = 0;
        curUrl.clear();
        restartScratch = false;
        w0done = false;
    }

    void finalize() {
        DlOutcome o;
        o.finalUrl = finalUrl.empty() ? P.url : finalUrl;
        o.contentType = contentType;
        o.connsUsed = connsUsed;
        o.chunked = mode == M_CHUNKED;
        o.bytes = done.load();
        o.stage = fstage; o.status = fstatus; o.sceError = fsce;
        if (P.discard) {
            o.result = fatal.load() ? fatalResult : DlResult::OK;
            if (cancel.load()) o.result = DlResult::CANCELLED;
            std::lock_guard<std::mutex> lk(outMu);
            out = o;
            return;
        }
        bool complete = false;
        if (mode == M_CHUNKED) complete = allChunksDone();
        else if (mode == M_OPEN || mode == M_STREAM200) complete = completedOpen;
        if (writerOpen) {
            if (complete && !fatal.load() && !cancel.load()) {
                if (!w.finish()) { setFatal(DlResult::DISK_ERROR, "disco", 0, 0); complete = false; }
            } else if (mode == M_CHUNKED) {
                w.waitWritten(w.lastSeq());
                w.finish();
            } else {
                w.abort();
            }
        }
        o.partPath = partPath; o.finalPath = finalPath; o.name = name;
        if (complete && !fatal.load() && !cancel.load()) {
            if (rename(partPath.c_str(), finalPath.c_str()) != 0) {
                o.result = DlResult::DISK_ERROR; o.stage = "no se pudo renombrar";
                if (mode != M_CHUNKED) remove(partPath.c_str());
            } else {
                if (!metaPath.empty()) remove(metaPath.c_str());
                o.result = DlResult::OK;
                if (mode == M_CHUNKED) o.bytes = total;
            }
        } else {
            o.result = cancel.load() ? DlResult::CANCELLED : (fatal.load() ? fatalResult : DlResult::NET_ERROR);
            o.stage = fstage; o.status = fstatus; o.sceError = fsce;
            const bool keep = writerOpen && mode == M_CHUNKED && (o.result == DlResult::CANCELLED || o.result == DlResult::NET_ERROR || o.result == DlResult::DISK_ERROR);
            if (keep) {
                std::lock_guard<std::mutex> lk(mu);
                saveMetaLocked();
                o.kept = true;
            } else if (writerOpen) {
                remove(partPath.c_str());
                if (!metaPath.empty()) remove(metaPath.c_str());
            }
        }
        std::lock_guard<std::mutex> lk(outMu);
        out = o;
    }

    void loadResume() {
        resumeCand = false;
        if (!P.resume || P.discard) return;
        for (const DlPartial& dp : dl_list_partials(P.dir)) {
            if (dp.url != P.url) continue;
            Meta m;
            long long sz = 0;
            if (!readMeta(dp.metaPath, m) || m.chunk != chunk || !fileSize(dp.partPath, sz) || sz != m.total) continue;
            rm = m;
            resumeCand = true;
            total = m.total;
            nChunks = static_cast<int>((m.total + m.chunk - 1) / m.chunk);
            st.assign(static_cast<size_t>(nChunks), 0);
            for (int i : m.done) if (i >= 0 && i < nChunks) st[static_cast<size_t>(i)] = 2;
            resumedFrom = metaDoneBytes(m);
            done = resumedFrom;
            resumed = true;
            if (resumedFrom >= m.total) {  // todos los trozos estaban completos: solo faltaba renombrar
                if (rename(dp.partPath.c_str(), m.finalPath.c_str()) == 0) {
                    remove(dp.metaPath.c_str());
                    alreadyDone = true;
                    finalPath = m.finalPath; partPath = dp.partPath;
                    name = finalPath.substr(finalPath.find_last_of('/') + 1);
                    total = m.total; mode = M_CHUNKED;
                    return;
                }
            }
            log("[dleng] se reanuda " + dp.finalPath + " desde " + std::to_string(resumedFrom) + " de " + std::to_string(m.total) + " bytes");
            return;
        }
    }

    void coordMain() {
        for (int attempt = 0; attempt < 2; attempt++) {
            if (attempt == 0) loadResume();
            if (alreadyDone) {
                DlOutcome o;
                o.result = DlResult::OK; o.finalPath = finalPath; o.partPath = partPath; o.name = name; o.bytes = total; o.chunked = true; o.finalUrl = P.url;
                { std::lock_guard<std::mutex> lk(outMu); out = o; }
                fin = true;
                return;
            }
            alive = 1;
            workers.emplace_back([this] { workerMain(true, 0); });
            while (!headersReady && !w0done.load() && !cancel.load() && !fatal.load()) sleepMs(5);
            int n = 1;
            {
                std::lock_guard<std::mutex> lk(mu);
                if (headersReady && !fatal.load() && !cancel.load() && mode == M_CHUNKED) {
                    int pending = 0;
                    for (uint8_t v : st) if (v == 0) pending++;
                    if (P.discard) pending = P.conns;
                    n = std::max(1, std::min({P.conns, 12, pending + 1}));
                }
            }
            ceiling = n;
            allowed = P.discard ? n : std::min(n, 2);   // el medidor usa un numero fijo; las descargas empiezan con 2 y suben
            for (int i = 1; i < n; i++) { ++alive; workers.emplace_back([this, i] { workerMain(false, i); }); }
            connsUsed = n;
            // supervision: subida gradual mientras la velocidad total crezca (ventanas de 4 s)
            {
                long long lastT = nowMs(), lastDone = done.load();
                double prev = -1;
                while (alive.load() > 0 && !cancel.load() && !fatal.load()) {
                    sleepMs(200);
                    if (P.discard || rampStopped.load() || n <= 1) continue;
                    const long long t = nowMs();
                    if (t - lastT < P.rampMs) continue;
                    const long long d = done.load();
                    const double sp = static_cast<double>(d - lastDone) * 1000.0 / static_cast<double>(t - lastT);
                    lastT = t; lastDone = d;
                    const int a = allowed.load();
                    if (!anyPending()) { rampStopped = true; continue; }   // ya no quedan trozos que repartir
                    if (prev < 0 || sp > prev * 1.12) {
                        if (a < ceiling.load()) {
                            const int na = std::min(ceiling.load(), a + (a >= 3 ? 2 : 1));
                            allowed = na;
                            log("[dleng] velocidad total " + std::to_string(static_cast<int>(sp / 1024)) + " KB/s con " + std::to_string(a) + " conexion(es): se sube a " + std::to_string(na));
                        } else rampStopped = true;
                        prev = sp;
                    } else {
                        if (sp < prev * 0.95 && a > 1) { allowed = a - 1; log("[dleng] mas conexiones no aceleran (" + std::to_string(static_cast<int>(sp / 1024)) + " KB/s): se vuelve a " + std::to_string(a - 1)); }
                        else log("[dleng] la velocidad ya no sube con mas conexiones (" + std::to_string(static_cast<int>(sp / 1024)) + " KB/s con " + std::to_string(a) + "): se mantiene");
                        rampStopped = true;
                    }
                }
            }
            allowed = std::max(allowed.load(), 1);
            for (auto& t : workers) t.join();
            workers.clear();
            if (restartScratch.load() && attempt == 0 && !cancel.load()) {
                log("[dleng] la reanudacion no es posible (el servidor o el archivo cambiaron): se empieza de cero");
                resetForRestart();
                continue;
            }
            break;
        }
        finalize();
        fin = true;
    }
};

// ------------------------------------------------------------------ API
DlEngine::DlEngine() : d_(new Impl()) {}
DlEngine::~DlEngine() { cancel(); join(); }

bool DlEngine::start(const DlParams& p) {
    Impl& e = *d_;
    if (!p.fetch || p.dir.empty()) return false;
    e.P = p;
    e.P.conns = std::max(1, std::min(12, p.conns));
    e.chunk = std::max<long long>(1 << 20, p.chunk);
    e.P.chunk = e.chunk;
    e.t0 = nowMs();
    e.coord = std::thread([&e] { e.coordMain(); });
    return true;
}
bool DlEngine::finished() const { return d_->fin.load(); }
void DlEngine::cancel() { d_->cancel = true; }
void DlEngine::join() { if (d_->coord.joinable()) d_->coord.join(); }
DlProgress2 DlEngine::progress() const {
    Impl& e = *d_;
    DlProgress2 p;
    p.done = e.done.load();
    p.activeConns = e.active.load();
    p.allowedConns = e.allowed.load();
    p.retryCount = e.retryCount.load();
    p.queuedBytes = e.w.queued();
    p.resumed = e.resumed;
    p.resumedFrom = e.resumedFrom;
    std::unique_lock<std::mutex> lk(e.mu, std::try_to_lock);
    p.total = lk.owns_lock() ? e.total : -1;
    return p;
}
DlOutcome DlEngine::outcome() const {
    std::lock_guard<std::mutex> lk(d_->outMu);
    return d_->out;
}

std::vector<DlPartial> dl_list_partials(const std::string& dir) {
    std::vector<DlPartial> v;
    const std::string idx = dir + "/.partials";
    const std::vector<std::string> all = indexRead(idx);
    std::vector<std::string> keep;
    for (const std::string& metaPath : all) {
        DlPartial p;
        p.metaPath = metaPath;
        if (metaPath.size() < 6) continue;
        p.partPath = metaPath.substr(0, metaPath.size() - 5);
        Meta m;
        long long sz = 0;
        if (!readMeta(p.metaPath, m) || !fileSize(p.partPath, sz)) continue;  // ya no existe (completada, borrada...): sale del indice
        p.url = m.url; p.finalPath = m.finalPath; p.total = m.total; p.done = metaDoneBytes(m);
        v.push_back(p);
        keep.push_back(metaPath);
    }
    if (keep.size() != all.size()) indexWrite(idx, keep);
    return v;
}
void dl_discard_partial(const DlPartial& p) {
    remove(p.partPath.c_str());
    remove(p.metaPath.c_str());
}
