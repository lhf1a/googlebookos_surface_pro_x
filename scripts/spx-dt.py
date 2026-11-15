"""Shared property snapshots for offline Surface device-tree edits."""
import libfdt


def properties(fdt):
    result = {}
    offset, depth = -1, 0
    while True:
        offset, depth = fdt.next_node(offset, depth)
        if offset < 0 or depth < 0:
            break
        path = fdt.get_path(offset)
        prop = fdt.first_property_offset(offset, quiet=(libfdt.NOTFOUND,))
        while prop >= 0:
            value = fdt.get_property_by_offset(prop)
            result[path, value.name] = bytes(value)
            prop = fdt.next_property_offset(prop, quiet=(libfdt.NOTFOUND,))
    return result

