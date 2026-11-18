// A set is a collection of asset references. Removing a card never removes an asset file.
struct TileSetInfo
{
    std::string name;
    std::vector<fs::path> members;
};
TileSetInfo *tileSetResult = nullptr;
int tileSetCursorValue()
{
    return tileSetsCallback(7, nullptr, 0, nullptr, nullptr, 0);
}
// This adapter only marshals UTF-8 strings across the ABI; membership and history are in Xenon.
int dispatchTileSet(int operation, const fs::path &set, const std::vector<fs::path> &paths,
                    const char *name = nullptr, int flags = 0)
{
    std::vector<std::string> text;
    for (const auto &path : paths)
    {
        text.push_back(utf8(path));
    }
    std::vector<const char *> pointers;
    for (const auto &value : text)
    {
        pointers.push_back(value.c_str());
    }
    return tileSetsCallback(operation, utf8(set).c_str(), (int)pointers.size(), pointers.data(), name, flags);
}
std::vector<fs::path> paletteSelection;
fs::path paletteSelectionSet;

bool tileAssetPath(const fs::path &relative)
{
    return dispatchTileSet(8, relative, {}) != 0;
}
bool tileSetRead(const fs::path &set, TileSetInfo &result)
{
    TileSetInfo value;
    tileSetResult = &value;
    bool ok = dispatchTileSet(0, set, {}) != 0;
    tileSetResult = nullptr;
    if (ok)
    {
        result = std::move(value);
    }
    return ok;
}
bool tileSetWrite(const fs::path &set, const TileSetInfo &value)
{
    return dispatchTileSet(1, set, value.members, value.name.c_str()) != 0;
}
bool tileSetChange(const fs::path &set, const std::vector<fs::path> &paths, bool add, bool history = true)
{
    if (!dispatchTileSet(add ? 2 : 3, set, paths, nullptr, history ? 1 : 0))
    {
        return false;
    }
    tileCatalogDirty = true;
    return true;
}
void tileSetUndo(bool redo)
{
    tilePaletteCallback(2, nullptr, redo ? 1 : 0);
}

void tileSetDropTarget()
{
    if (tileSet.empty() || !ImGui::GetDragDropPayload())
    {
        return;
    }
    // Use the whole Tiles window, including empty space and existing cards.
    ImRect bounds(ImGui::GetWindowPos(), {ImGui::GetWindowPos().x + ImGui::GetWindowSize().x,
                                          ImGui::GetWindowPos().y + ImGui::GetWindowSize().y});
    if (ImGui::BeginDragDropTargetCustom(bounds, ImGui::GetID("Tileset drop")))
    {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("OXYGEN_TILE_ASSET"))
        {
            if (payload->DataSize == sizeof(int))
            {
                tilePaletteCallback(1, nullptr, *(const int *)payload->Data);
            }
        }
        ImGui::EndDragDropTarget();
    }
}
