"""Lossless DTB-to-DTS source export: property bytes, not inferred C strings.

dtc can render 00 32 4b 00 as "\\02K", which re-parses as octal 02, 4b, 00.
Explicit byte arrays preserve cells, strings, phandles and unknown properties.
"""
import re


def from_dtb(data):
    import libfdt
    fdt=libfdt.Fdt(data)
    lines=['/dts-v1/;','// Property bytes are explicit to avoid ambiguous string escapes.']
    for i in range(fdt.num_mem_rsv()):
        reservation=fdt.get_mem_rsv(i)
        if len(reservation)==3:
            status,address,size=reservation
            if status:raise ValueError('Could not read DT memory reservation')
        else:
            address,size=reservation
        lines.append('/memreserve/ 0x%x 0x%x;' % (address,size))
    def identifier(name):
        if not re.fullmatch(r'[A-Za-z0-9,._+*#?@-]+',name):
            raise ValueError('Unsupported DTS identifier: '+name)
        return name
    def first(function,*args):
        try:return function(*args)
        except libfdt.FdtException as exc:
            if exc.err != -libfdt.NOTFOUND:raise
            return -1
    def node(offset,depth):
        indent='\t'*depth
        name='/' if depth==0 else identifier(fdt.get_name(offset))
        lines.append(indent+name+' {')
        p=first(fdt.first_property_offset,offset)
        while p>=0:
            prop=fdt.get_property_by_offset(p)
            value=bytes(prop)
            suffix=' = ['+value.hex(' ')+'];' if value else ';'
            lines.append(indent+'\t'+identifier(prop.name)+suffix)
            p=first(fdt.next_property_offset,p)
        child=first(fdt.first_subnode,offset)
        while child>=0:
            node(child,depth+1)
            child=first(fdt.next_subnode,child)
        lines.append(indent+'};')
    node(0,0)
    return '\n'.join(lines)+'\n'
