def slurp_file(file_or_path, mode="r"):
    """Open file for reading and return contents."""
    with open(str(file_or_path), mode) as fp:
        return fp.read().strip()
