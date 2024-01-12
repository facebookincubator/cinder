chkdict = dict
chklist = list

from _static import __build_cinder_class__ as __build_cinder_class__

def native(so_path):
    def _inner_native(func):
        return func
    return _inner_native
