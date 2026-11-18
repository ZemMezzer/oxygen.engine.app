// ImGui ABI: widgets and input queries only; no scene, layer or asset policy.
EDITOR_API int ui_button(const char *label, float width, float height)
{
    return ImGui::Button(label, {width, height});
}
EDITOR_API int ui_query(int query)
{
    const auto &io = ImGui::GetIO();
    switch (query)
    {
    case 0:
        return ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    case 1:
        return io.WantTextInput;
    case 2:
        return ImGui::IsAnyItemActive();
    case 3:
        return ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    case 4:
        return io.KeyCtrl || io.KeySuper;
    case 5:
        return io.KeyShift;
    case 6:
        return ImGui::IsWindowAppearing();
    default:
        return 0;
    }
}
EDITOR_API int ui_key(int key)
{
    return ImGui::IsKeyPressed(key == KEY_ENTER ? ImGuiKey_Enter : keyFor(key), false);
}
EDITOR_API int ui_input_text(const char *label, char *value, int capacity, int flags)
{
    return ImGui::InputText(label, value, capacity,
                            (flags & 1 ? ImGuiInputTextFlags_EnterReturnsTrue : 0) |
                                (flags & 2 ? ImGuiInputTextFlags_AutoSelectAll : 0));
}
EDITOR_API int ui_popup_begin(const char *title)
{
    return ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize);
}
EDITOR_API void ui_popup_open(const char *title)
{
    ImGui::OpenPopup(title);
}
EDITOR_API void ui_popup_close()
{
    ImGui::CloseCurrentPopup();
}
EDITOR_API void ui_popup_end()
{
    ImGui::EndPopup();
}
EDITOR_API void ui_same_line()
{
    ImGui::SameLine();
}
EDITOR_API void ui_separator()
{
    ImGui::Separator();
}
EDITOR_API void ui_text(const char *text)
{
    ImGui::TextUnformatted(text);
}
EDITOR_API void ui_focus_keyboard()
{
    ImGui::SetKeyboardFocusHere();
}
EDITOR_API void ui_disabled_begin(int disabled)
{
    ImGui::BeginDisabled(disabled != 0);
}
EDITOR_API void ui_disabled_end()
{
    ImGui::EndDisabled();
}
EDITOR_API int ui_combo_begin(const char *label, const char *preview)
{
    return ImGui::BeginCombo(label, preview);
}
EDITOR_API void ui_combo_end()
{
    ImGui::EndCombo();
}
EDITOR_API int ui_selectable(const char *label, int selected)
{
    return ImGui::Selectable(label, selected != 0);
}
EDITOR_API void ui_push_id(int id)
{
    ImGui::PushID(id);
}
EDITOR_API void ui_pop_id()
{
    ImGui::PopID();
}
EDITOR_API int ui_radio(const char *label, int selected)
{
    return ImGui::RadioButton(label, selected != 0);
}
EDITOR_API int ui_checkbox(const char *label, int *value)
{
    bool checked = *value != 0;
    bool changed = ImGui::Checkbox(label, &checked);
    *value = checked ? 1 : 0;
    return changed;
}
EDITOR_API int ui_table_begin(const char *label, int columns, int flags)
{
    return ImGui::BeginTable(label, columns,
                             flags == 2   ? 0
                             : flags == 1 ? ImGuiTableFlags_SizingStretchSame
                                          : ImGuiTableFlags_SizingStretchProp);
}
EDITOR_API void ui_table_column(const char *name, float width)
{
    ImGui::TableSetupColumn(
        name, width > 0 ? ImGuiTableColumnFlags_WidthFixed : ImGuiTableColumnFlags_WidthStretch, width);
}
EDITOR_API void ui_table_headers()
{
    ImGui::TableHeadersRow();
}
EDITOR_API void ui_table_row()
{
    ImGui::TableNextRow();
}
EDITOR_API void ui_table_next()
{
    ImGui::TableNextColumn();
}
EDITOR_API void ui_table_end()
{
    ImGui::EndTable();
}

EDITOR_API void ui_section(const char *label)
{
    ImGui::SeparatorText(label);
}
EDITOR_API void ui_text_disabled(const char *label)
{
    ImGui::TextDisabled("%s", label);
}
EDITOR_API void ui_item_width(float width)
{
    ImGui::SetNextItemWidth(width);
}
EDITOR_API void ui_input_float(const char *label, float *value)
{
    ImGui::InputFloat(label, value, 0, 0, "%.3f");
}
EDITOR_API int ui_item_query(int query)
{
    switch (query)
    {
    case 0:
        return ImGui::IsItemActivated();
    case 1:
        return ImGui::IsItemEdited();
    case 2:
        return ImGui::IsItemActive();
    case 3:
        return ImGui::IsItemDeactivatedAfterEdit();
    case 4:
        return ImGui::IsItemClicked();
    case 5:
        return ImGui::IsItemToggledOpen();
    case 6:
        return ImGui::IsItemVisible();
    case 7:
        return ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
    default:
        return 0;
    }
}

EDITOR_API int ui_tree(const char *id, const char *label, int leaf, int selected)
{
    return ImGui::TreeNodeEx(id,
                             ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
                                 (leaf ? ImGuiTreeNodeFlags_Leaf : 0) |
                                 (selected ? ImGuiTreeNodeFlags_Selected : 0),
                             "%s", label);
}
EDITOR_API void ui_tree_pop()
{
    ImGui::TreePop();
}
EDITOR_API void ui_next_open()
{
    ImGui::SetNextItemOpen(true);
}
EDITOR_API void ui_scroll_here()
{
    ImGui::SetScrollHereY(.5f);
}
EDITOR_API void ui_drag_source(const char *type, int value, const char *label)
{
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
    {
        ImGui::SetDragDropPayload(type, &value, sizeof(value));
        ImGui::TextUnformatted(label);
        ImGui::EndDragDropSource();
    }
}
EDITOR_API int ui_drag_target(const char *type, int *value)
{
    bool accepted = false;
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(type))
        {
            if (payload->DataSize == sizeof(int))
            {
                *value = *(const int *)payload->Data;
                accepted = true;
            }
        }
        ImGui::EndDragDropTarget();
    }
    return accepted;
}
EDITOR_API int ui_context_begin()
{
    return ImGui::BeginPopupContextItem();
}
EDITOR_API int ui_menu_item(const char *label, int enabled)
{
    return ImGui::MenuItem(label, nullptr, false, enabled != 0);
}

EDITOR_API void ui_child_begin(const char *label, float width, float height, int border)
{
    ImGui::BeginChild(label, {width, height}, border ? ImGuiChildFlags_Borders : 0);
}
EDITOR_API void ui_child_end()
{
    ImGui::EndChild();
}
EDITOR_API float ui_available_width()
{
    return ImGui::GetContentRegionAvail().x;
}
EDITOR_API int ui_card_button(const char *label, float width, float height, int selected)
{
    if (selected)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.16f, .42f, .38f, 1));
    }
    bool clicked = ImGui::Button(label, {width, height});
    if (selected)
    {
        ImGui::PopStyleColor();
    }
    return clicked;
}
EDITOR_API void ui_text_wrapped(const char *text)
{
    ImGui::TextWrapped("%s", text);
}

EDITOR_API int ui_fps()
{
    return GetFPS();
}

EDITOR_API void ui_window_focus()
{
    ImGui::SetWindowFocus();
}
EDITOR_API int ui_image_button(const char *label, unsigned texture, float width, float height, int selected)
{
    if (selected)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.16f, .42f, .38f, 1));
    }
    bool clicked = ImGui::ImageButton(label, (ImTextureID)texture, {width, height}, {0, 1}, {1, 0});
    if (selected)
    {
        ImGui::PopStyleColor();
    }
    return clicked;
}

EDITOR_API int ui_header(const char *label)
{
    return ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);
}
EDITOR_API int ui_float4(const char *label, float *values)
{
    return ImGui::InputFloat4(label, values);
}
EDITOR_API int ui_drag_float(const char *label, float *value, float speed, float minimum, float maximum)
{
    return ImGui::DragFloat(label, value, speed, minimum, maximum);
}

EDITOR_API int ui_tab_bar_begin(const char *label)
{
    return ImGui::BeginTabBar(label);
}
EDITOR_API void ui_tab_bar_end()
{
    ImGui::EndTabBar();
}
EDITOR_API int ui_tab_begin(const char *label)
{
    return ImGui::BeginTabItem(label);
}
EDITOR_API void ui_tab_end()
{
    ImGui::EndTabItem();
}
EDITOR_API int ui_int2(const char *label, int *values)
{
    return ImGui::InputInt2(label, values);
}
EDITOR_API int ui_float2(const char *label, float *values)
{
    return ImGui::InputFloat2(label, values);
}
EDITOR_API void ui_slider_float(const char *label, float *value, float low, float high)
{
    ImGui::SliderFloat(label, value, low, high, "%.1fx");
}
