#include <bl_ui/layout.h>

namespace bl_ui {

OperatorItem& Layout::operator_(std::string_view idname, std::string_view text,
                                int icon_id, std::string_view shortcut) {
    _items.push_back(OperatorItem{std::string(idname), std::string(text),
                                  std::string(shortcut), icon_id, {}});
    return std::get<OperatorItem>(_items.back());
}

MenuRefItem& Layout::menu(std::string_view menu_idname, std::string_view text, int icon_id) {
    _items.push_back(MenuRefItem{std::string(menu_idname), std::string(text), icon_id});
    return std::get<MenuRefItem>(_items.back());
}

void Layout::separator() {
    _items.push_back(SeparatorItem{});
}

void Layout::label(std::string_view text) {
    _items.push_back(LabelItem{std::string(text)});
}

} // namespace bl_ui
