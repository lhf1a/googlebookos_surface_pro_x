"""Known Android/QTI input ABI adaptation; equivalent source diff is in patches/.

This is NOT a claimed source rebuild. Only original instructions/string or the
already adapted form are accepted; retain all normal/error/ownership branches.
"""


def patch(data):
    if data[:6]!=b'\x7fELF\x02\x01' or int.from_bytes(data[18:20],'little')!=183:
        raise ValueError('Expected AArch64 QTI composer ELF')
    entries=[(414820,bytes.fromhex('0ddc40f9bf0108eb'),bytes.fromhex('e803012a51000014')),
             (437144,bytes.fromhex('092140b94b2140b93f010b6b'),bytes.fromhex('7f00046be0179f1a16000014')),
             (158519,b'mdss_mdp/drm/card',b'/drm/card'+b'\0'*8)]
    for off,old,new in entries:
        if data[off:off+len(old)] not in (old,new):
            raise ValueError('Unsupported composer binary layout; rebuild/adapt its source ABI')
    out=bytearray(data)
    for off,old,new in entries:out[off:off+len(old)]=new
    return bytes(out)
