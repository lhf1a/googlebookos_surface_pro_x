"""Locate exact VIDEOCC reference cells, never search/replace matching numbers."""
import struct


def property_offsets(blob):
    if len(blob)<40:
        raise ValueError('Short FDT header')
    magic,total,off,strings,_,version,_,_,string_bytes,structure_bytes=struct.unpack_from('>10I',blob)
    if magic!=0xd00dfeed or total!=len(blob) or version<17:
        raise ValueError('Invalid FDT header')
    end=off+structure_bytes
    if off<40 or end>total or strings+string_bytes>total:
        raise ValueError('Invalid FDT bounds')
    result={}; stack=[]
    def word():
        nonlocal off
        if off+4>end:raise ValueError('Truncated FDT token')
        value=struct.unpack_from('>I',blob,off)[0];off+=4;return value
    while off<end:
        token=word()
        if token==1:
            zero=blob.find(b'\0',off,end)
            if zero<0:raise ValueError('Invalid node name')
            stack.append(blob[off:zero].decode('ascii'));off=(zero+4)&~3
        elif token==2:
            if not stack:raise ValueError('Unbalanced node')
            stack.pop()
        elif token==3:
            length=word();nameoff=word()
            if not stack or off+length>end or nameoff>=string_bytes:raise ValueError('Invalid property')
            zero=blob.find(b'\0',strings+nameoff,strings+string_bytes)
            if zero<0:raise ValueError('Invalid property name')
            name=blob[strings+nameoff:zero].decode('ascii')
            key=('/'+'/'.join(stack[1:]),name)
            if key in result:raise ValueError('Duplicate property')
            result[key]=(off,blob[off:off+length]);off=(off+length+3)&~3
        elif token==4:continue
        elif token==9:
            if stack:raise ValueError('Unbalanced FDT end')
            return result
        else:raise ValueError('Unknown FDT token')
    raise ValueError('Missing FDT end')


def videocc_offsets(blob,node,vid,gcc):
    properties=property_offsets(blob)
    expected={
        'power-domains':([vid,0,vid,1,vid,2],[0,2,4]),
        'clocks':([gcc,227,gcc,225,gcc,226,vid,4,vid,2,vid,3,vid,0],[6,8,10,12]),
        'resets':([gcc,41,vid,0,gcc,42,gcc,43],[2]),
    }
    offsets=[]
    for name,(cells,indices) in expected.items():
        offset,data=properties[(node,name)]
        if data!=struct.pack('>'+str(len(cells))+'I',*cells):
            raise ValueError('Unexpected '+name+' reference topology')
        offsets.extend(offset+4*i for i in indices)
    return sorted(offsets)
