module;

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

export module Editor.hierarchy;

import Editor.context;
import Engine.Render.entity;

export class Outliner {
  public:
    void draw(EditorContext& ctx);
    void focusSearch() { m_focusSearch = true; }
    void reveal(EditorContext& ctx, EntityHandle e);

  private:
    struct Row {
        EntityHandle entity;
        uint16_t depth = 0;
        bool hasChildren = false;
    };

    struct Query {
        std::string text;
        std::string tag;
        std::string mesh;
        int layer = -1;
        int visible = -1;
        bool selectedOnly = false;
        bool active = false;
    };

    void rebuild(EditorContext& ctx);
    void runSearch(EditorContext& ctx);
    [[nodiscard]] bool matches(EditorContext& ctx, EntityHandle e) const;
    void drawRow(EditorContext& ctx, const Row& row, int index);
    void parseQuery();

    std::vector<Row> m_rows;
    size_t m_totalRoots = 0;
    bool m_truncated = false;
    uint64_t m_builtStructure = 0;
    uint64_t m_builtExpansion = 1;
    uint64_t m_expansionVersion = 0;
    double m_lastBuild = -1.0;
    std::unordered_set<uint32_t> m_expanded;
    std::string m_search;
    std::string m_lastSearch;
    Query m_query;
    Entity m_searchCursor = 0;
    std::vector<EntityHandle> m_searchResults;
    bool m_searchDone = true;
    bool m_focusSearch = false;
    EntityHandle m_renaming;
    std::string m_renameBuffer;
    bool m_renameFocus = false;
    EntityHandle m_scrollTo;
    EntityHandle m_rangeAnchor;
};
