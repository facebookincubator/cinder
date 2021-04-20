#ifndef phmap_example_hash_value_
#define phmap_example_hash_value_

#include <parallel_hashmap/phmap_utils.h> // minimal header providing phmap::HashState()
#include <string>
using std::string;

struct Person
{
    bool operator==(const Person &o) const
    { 
        return _first == o._first && _last == o._last && _age == o._age; 
    }

    // Demonstrates how to provide the hash function as a friend member function of the class
    // This can be used as an alternative to providing a std::hash<Person> specialization
    // --------------------------------------------------------------------------------------
    friend size_t hash_value(const Person &p) 
    {
            return phmap::HashState().combine(0, p._first, p._last, p._age);
    }

    string _first;
    string _last;
    int    _age;
};

#endif // phmap_example_hash_value_
