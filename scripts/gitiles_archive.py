#!/usr/bin/env python3
"""Verify a Gitiles archive by Git tree content, independent of gzip metadata."""
import hashlib
from pathlib import PurePosixPath


def object_id(kind, data):
    return hashlib.sha1(kind.encode() + b" " + str(len(data)).encode() + b"\0" + data).digest()


def archive_tree(tar):
    root = {}
    seen = set()
    for member in tar.getmembers():
        name = member.name.rstrip("/") if member.isdir() else member.name
        path = PurePosixPath(name)
        if (not name or path.is_absolute() or ".." in path.parts or
                str(path) != name or name in seen):
            raise ValueError("Unsafe or duplicate archive path: " + member.name)
        seen.add(name)
        node = root
        for part in path.parts[:-1]:
            node = node.setdefault(part, {})
            if not isinstance(node, dict):
                raise ValueError("Archive file/directory conflict: " + name)
        leaf = path.name
        if member.isdir():
            if not isinstance(node.setdefault(leaf, {}), dict):
                raise ValueError("Archive directory/file conflict: " + name)
            continue
        if leaf in node:
            raise ValueError("Archive file/directory conflict: " + name)
        if member.isfile():
            data = tar.extractfile(member).read()
            mode = b"100755" if member.mode & 0o111 else b"100644"
        elif member.issym():
            # Extraction additionally uses tarfile's data filter.
            data = member.linkname.encode("utf-8")
            mode = b"120000"
        else:
            raise ValueError("Unsupported archive entry: " + name)
        node[leaf] = (mode, object_id("blob", data))

    def tree(node):
        entries = []
        for name, value in node.items():
            encoded = name.encode("utf-8")
            directory = isinstance(value, dict)
            mode, digest = (b"40000", tree(value)) if directory else value
            entries.append((encoded + (b"/" if directory else b""),
                            mode + b" " + encoded + b"\0" + digest))
        return object_id("tree", b"".join(value for _, value in sorted(entries)))
    return tree(root).hex()
