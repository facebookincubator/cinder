/*
 * Make sure that the phmap.h header builds fine when included in two separate 
 * source files
 */
#include <string>
#include <parallel_hashmap/phmap.h>

using phmap::flat_hash_map;
 
int main()
{
    // Create an unordered_map of three strings (that map to strings)
    using Map = flat_hash_map<std::string, std::string>;
    Map email = 
    {
        { "tom",  "tom@gmail.com"},
        { "jeff", "jk@gmail.com"},
        { "jim",  "jimg@microsoft.com"}
    };
 
    extern void f2(Map&);
    f2(email);
 
    return 0;
}
