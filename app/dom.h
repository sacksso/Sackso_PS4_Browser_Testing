#pragma once
// Arbol DOM minimo + selectores + "CSS lite" (v0.9.0).
//   - Dom::parse: HTML tolerante -> arbol (entidades decodificadas en texto y atributos).
//   - Dom::serialize: arbol -> HTML para el maquetador existente (htmldoc.cpp). Aplica CSS lite: los elementos con
//     display:none / visibility:hidden (hoja <style>, <link> descargadas o atributo style) se omiten y 'color' se pasa en linea.
//   - Selectores: tag, *, #id, .clase, [attr], [attr=v ~= ^= $= *=], :not(), :first-child, :last-child, :nth-child(n|odd|even),
//     combinadores ' ', '>', '+', '~' y listas con comas. Pseudoclases dinamicas (:hover...) nunca coinciden.
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct DomNode {
    enum Type { ELEMENT = 1, TEXT = 3, COMMENT = 8, DOCUMENT = 9, FRAGMENT = 11 };
    Type type = ELEMENT;
    int id = 0;                 // indice estable en Dom::nodes (lo usa JavaScript)
    std::string tag;            // en minusculas ("" para texto/comentario)
    std::string text;           // texto (ya decodificado) o contenido bruto de script/style
    std::vector<std::pair<std::string, std::string>> attrs;
    std::vector<DomNode*> kids;
    DomNode* parent = nullptr;
};

class Dom {
public:
    Dom();
    ~Dom();
    Dom(const Dom&) = delete;
    Dom& operator=(const Dom&) = delete;

    void parse(const std::string& html);  // sustituye todo el contenido
    // Serializa el documento para maquetar. applyCss: omite ocultos y aplica color. dropNoscript: quita <noscript> (cuando hay JS).
    std::string serialize(bool applyCss, bool dropNoscript);

    DomNode* root() { return nodes_[0].get(); }   // DOCUMENT
    DomNode* node(int id) { return id >= 0 && id < static_cast<int>(nodes_.size()) ? nodes_[id].get() : nullptr; }
    DomNode* html();       // <html> (puede ser null)
    DomNode* head();
    DomNode* body();
    DomNode* createElement(const std::string& tag);
    DomNode* createText(const std::string& s);
    DomNode* createFragment();
    DomNode* clone(DomNode* n, bool deep);

    // modificaciones (marcan 'dirty')
    void append(DomNode* parent, DomNode* child);
    void insertBefore(DomNode* parent, DomNode* child, DomNode* ref);  // ref null = al final
    void remove(DomNode* child);
    void setText(DomNode* n, const std::string& s);       // textContent = s
    void setInnerHtml(DomNode* n, const std::string& html);
    void setAttr(DomNode* n, const std::string& name, const std::string& v);
    void removeAttr(DomNode* n, const std::string& name);
    static const std::string* getAttr(const DomNode* n, const std::string& name);

    // consultas
    static std::string textContent(const DomNode* n);
    std::string innerHtml(const DomNode* n);
    std::string outerHtml(const DomNode* n);
    DomNode* byId(const std::string& id);
    std::vector<DomNode*> query(DomNode* root, const std::string& selector, bool firstOnly, bool* syntaxOk);
    bool matches(DomNode* n, const std::string& selector);
    DomNode* closest(DomNode* n, const std::string& selector);

    // hojas de estilo
    void addStyleSheet(const std::string& css);          // se suma a las de <style> (p. ej. <link> descargado)
    std::vector<std::string> linkedStyleHrefs();         // href de <link rel=stylesheet> en orden
    bool dirty = false;                                   // cambió desde la última serialización
    int styleRevision = 0;

    struct Rules;  // reglas CSS analizadas (interno)
private:
    std::vector<std::unique_ptr<DomNode>> nodes_;
    std::vector<std::string> extraCss_;
    std::shared_ptr<Rules> rules_;  // cache de reglas CSS analizadas
    int rulesRev_ = -1;
    DomNode* newNode(DomNode::Type t);
    bool styleDirty_ = true;
    void rebuildRules();
    void touchStyle(const DomNode* n);
    void ensureStructure();
    void parseInto(DomNode* container, const std::string& html);
    void ser(const DomNode* n, std::string& out, int mode, bool dropNoscript);
};

// Utilidades de texto compartidas
std::string domDecodeEntities(const std::string& s);
std::string domEscapeText(const std::string& s);
std::string domEscapeAttr(const std::string& s);
