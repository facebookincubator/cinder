#ifndef phmap_example_hash_std_
#define phmap_example_hash_std_

#include <parallel_hashmap/phmap_utils.h> // minimal header providing phmap::HashState()
#include <string>
using std::string;

struct Person
{
    bool operator==(const Person &o) const
    { 
        return _first == o._first && _last == o._last && _age == o._age; 
    }

    string _first;
    string _last;
    int    _age;
};

namespace std
{
    // inject specialization of std::hash for Person into namespace std
    // An alternative is to provide a hash_value() friend function (see hash_value.h)
    // ------------------------------------------------------------------------------
    template<> struct hash<Person>
    {
        std::size_t operator()(Person const &p) const
        {
            return phmap::HashState().combine(0, p._first, p._last, p._age);
        }
    };
}

#endif // phmap_example_hash_std_
