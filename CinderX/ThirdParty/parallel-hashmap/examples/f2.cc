/*
 * Make sure that the phmap.h header builds fine when included in two separate 
 * source files
 */
#include <iostream>
#include <string>
#include <parallel_hashmap/phmap.h>

using phmap::flat_hash_map;
using Map = flat_hash_map<std::string, std::string>;

void f2(Map& email)
{
    // Iterate and print keys and values 
    for (const auto& n : email) 
        std::cout << n.first << "'s email is: " << n.second << "\n";
 
    // Add a new entry
    email["bill"] = "bg@whatever.com";
 
    // and print it
    std::cout << "bill's email is: " << email["bill"] << "\n";
}
