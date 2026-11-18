int tileSetTests(const fs::path &existingTile)
{
    int failures = 0;
    fs::path destination = fs::path("Tiles") / "tile_MembershipTest";
    fs::create_directories(root / "Assets" / destination);
    TileSetInfo empty{"Membership", {}};
    if (!tileSetWrite(destination, empty) || !tileSetChange(destination, {existingTile}, true))
    {
        failures++;
    }
    TileSetInfo loaded;
    if (!tileSetRead(destination, loaded) || loaded.members != std::vector<fs::path>{existingTile})
    {
        failures++;
    }
    int historyCount = tileSetCursorValue();
    if (!tileSetChange(destination, {existingTile}, true) || tileSetCursorValue() != historyCount)
    {
        failures++;
    }
    if (!tileSetChange(destination, {existingTile}, false) || !tileSetRead(destination, loaded) ||
        !loaded.members.empty() || !fs::is_regular_file(root / "Assets" / existingTile))
    {
        failures++;
    }
    tileSetUndo(false);
    if (!tileSetRead(destination, loaded) || loaded.members.size() != 1)
    {
        failures++;
    }
    tileSetUndo(true);
    if (!tileSetRead(destination, loaded) || !loaded.members.empty())
    {
        failures++;
    }
    if (tileSetChange(destination, {fs::path("../outside.oxtile")}, true))
    {
        failures++;
    }
    // Explicit membership persists across a rescan, including an empty collection.
    tileCatalogDirty = true;
    tileScan();
    for (const auto &card : tileCards)
    {
        if (card.set == destination)
        {
            failures++;
        }
    }
    // An imported tile can be referenced by another set without changing its path.
    if (!tileSetChange(destination, {existingTile}, true))
    {
        failures++;
    }
    tileScan();
    int found = 0;
    for (const auto &card : tileCards)
    {
        if (card.set == destination && card.relative == existingTile)
        {
            found++;
        }
    }
    if (found != 1)
    {
        failures++;
    }
    // v1 collection migration preserves unrelated existing members.
    TileSetInfo legacy;
    if (!tileSetRead(existingTile.parent_path(), legacy) ||
        std::find(legacy.members.begin(), legacy.members.end(), existingTile) == legacy.members.end())
    {
        failures++;
    }
    if (!tileSetChange(existingTile.parent_path(), {existingTile}, false) ||
        !tileSetRead(existingTile.parent_path(), legacy) ||
        std::find(legacy.members.begin(), legacy.members.end(), existingTile) != legacy.members.end())
    {
        failures++;
    }
    tileSetUndo(false);
    puts(failures
             ? "FAIL: tileset membership."
             : "PASS: tileset add/remove/Undo, persistence, duplicate suppression and asset preservation.");
    return failures;
}
