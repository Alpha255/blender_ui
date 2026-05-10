#pragma once
#include "../render/roundbox.h"
#include "../render/font.h"
#include <string>
#include <vector>
#include <memory>

namespace bl_ui {

// ---------------------------------------------------------------------------
// ScenePanel — right-side floating overlay showing the scene/collection tree.
//
// Matches Blender's Outliner header in the 3D Viewport sidebar (N-panel area):
//   • Floating panel anchored to the right edge, below the menu bar.
//   • Tree nodes: Scene → Collection → objects (mesh, camera, light, empty).
//   • Collapse/expand arrows for nodes with children.
//   • Active-tool highlight in blue; hover effect on mouse over.
//   • Eye icon (visibility toggle) on the right of each row.
//   • Scroll wheel support for long lists.
//
// Input priority: left-button clicks inside the panel are consumed; the
// viewport receives all events to the left of the panel.
// ---------------------------------------------------------------------------

class ScenePanel {
public:
    enum class NodeType { Scene, Collection, Mesh, Camera, Light, Empty };

    struct SceneNode {
        std::string name;
        NodeType    type     = NodeType::Mesh;
        bool        expanded = true;
        bool        visible  = true;
        std::vector<std::unique_ptr<SceneNode>> children;
    };

    ScenePanel();

    // Called once after Roundbox and Font are ready (App constructor body).
    void set_dependencies(Roundbox* rb, Font* font);

    void set_viewport(float w, float h);

    // Render the overlay. Called every frame from App::run().
    void draw(float header_h);

    // Returns true when the cursor is over the panel (event consumed).
    bool handle_mouse(float mx, float my, bool pressed, bool released);

    // Scroll the item list (dy > 0 = scroll up, i.e. dy from GLFW is inverted).
    void handle_scroll(float dy);

    // Left edge of the panel — use to limit viewport picking.
    float panel_left() const { return _px; }

private:
    struct FlatItem {
        SceneNode* node;
        int        depth;
        float      y0;     // top of row in screen coords (before scroll applied)
    };

    void _build_scene();
    void _flatten(SceneNode* node, int depth, float& cursor_y);
    void _draw_item(int idx, bool hovered, bool selected);

    static RGBA _type_color(NodeType t);
    static float _panel_header_h();

    float _vp_w = 800.f, _vp_h = 600.f;

    // Panel bounding box (recomputed each frame in draw())
    float _px = 0.f, _py = 0.f;
    float _pw = 200.f, _ph = 0.f;

    float _scroll    = 0.f;   // pixels scrolled down (≥ 0)
    float _content_h = 0.f;   // total height of all flattened items

    std::unique_ptr<SceneNode> _root;
    std::vector<FlatItem>      _flat;  // rebuilt every frame

    int _hov_item = -1;
    int _sel_item = -1;

    Roundbox* _rb   = nullptr;
    Font*     _font = nullptr;
};

} // namespace bl_ui
