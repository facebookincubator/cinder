/* immutable_globals state */

extern int __immutable_globals_creation;
extern int __immutable_globals_detection;

static inline void set_immutable_globals_immutable_creation(int flag) {__immutable_globals_creation = flag;}
static inline void set_immutable_globals_immutable_detection(int flag) {__immutable_globals_detection = flag;}
