// Generic filesystem bindings. Policy, formats and history are implemented in Xenon.
EDITOR_API int ed_fs_list(const char *directory, int recursive, void (*visitor)(const char *, int))
{
    try
    {
        auto visit = [&](const fs::directory_entry &entry) {
            int type = entry.is_symlink() ? 3 : entry.is_directory() ? 2 : entry.is_regular_file() ? 1 : 0;
            visitor(utf8(entry.path()).c_str(), type);
        };
        if (recursive)
        {
            for (const auto &entry : fs::recursive_directory_iterator(
                     fromUtf8(directory), fs::directory_options::skip_permission_denied))
            {
                visit(entry);
            }
        }
        else
        {
            for (const auto &entry : fs::directory_iterator(fromUtf8(directory)))
            {
                visit(entry);
            }
        }
        return 1;
    }
    catch (...)
    {
        return 0;
    }
}

EDITOR_API int ed_fs_path(int operation, const char *path, const char *base, char *output, int capacity)
{
    try
    {
        fs::path value = fromUtf8(path);
        if (operation == 0)
        {
            value = fs::absolute(value).lexically_normal();
        }
        if (operation == 1)
        {
            value = value.lexically_normal();
        }
        if (operation == 2)
        {
            value = value.lexically_relative(fromUtf8(base));
        }
        if (operation == 3)
        {
            value = fs::weakly_canonical(value);
        }
        std::string text = utf8(value);
        if ((int)text.size() >= capacity)
        {
            return 0;
        }
        std::copy(text.c_str(), text.c_str() + text.size() + 1, output);
        return 1;
    }
    catch (...)
    {
        return 0;
    }
}
EDITOR_API int ed_fs_create_directories(const char *path)
{
    try
    {
        fs::create_directories(fromUtf8(path));
        return 1;
    }
    catch (...)
    {
        return 0;
    }
}
EDITOR_API int ed_fs_copy(const char *source, const char *destination)
{
    try
    {
        return fs::copy_file(fromUtf8(source), fromUtf8(destination), fs::copy_options::none);
    }
    catch (...)
    {
        return 0;
    }
}
EDITOR_API int ed_fs_symlink(const char *path)
{
    try
    {
        return fs::is_symlink(fromUtf8(path));
    }
    catch (...)
    {
        return 1;
    }
}
EDITOR_API const char *ed_project_root()
{
    static std::string text;
    text = utf8(root);
    return text.c_str();
}
