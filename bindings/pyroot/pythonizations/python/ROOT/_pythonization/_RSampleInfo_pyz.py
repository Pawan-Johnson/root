import numba as nb
def make_string_from_array(ptr, size):
    arr = nb.carray(ptr, (size, ))
    string = ""
    for i in range(size):
        string = string + chr(arr[i])
    return string
make_string_from_array= nb.njit(nb.types.unicode_type(nb.types.CPointer(nb.types.int8), nb.types.int64))(make_string_from_array)


spec = [
    ("fID", nb.types.unicode_type),
    ("fEntryRange", nb.types.UniTuple(nb.types.uint64, 2))
]

@nb.experimental.jitclass(spec)
class RSampleInfo():
    # ! TODO: Overload Constructors?
    def __init__(self, fID, fEntryRange):
        self.fID = fID
        self.fEntryRange = fEntryRange

    def Contains(self, substr: str) -> bool:
        return substr in self.fID
    
    def Empty(self) -> bool:
        return bool(self.fID)
    
    def AsString(self)->str:
        return self.fID

    def EntryRange(self) -> tuple:
        return self.fEntryRange
    
    def NEntries(self):
        return self.fEntryRange[1] - self.fEntryRange[0]
    
    def ___eq__(self, o):
        return self.fID == o.fID
    
    def __ne__(self, o):
        return not self.__eq__(o)

def numba_type_of_RSampleInfo():
    a = RSampleInfo("Meow", (1111,2222))
    return nb.typeof(a)