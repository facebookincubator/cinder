try:
    import cinderjit
except:
    cinderjit = None
from staticmod import caller

caller  # force import of caller
if cinderjit is not None:
    cinderjit.disable()

print(caller(1))
