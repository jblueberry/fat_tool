#include <iostream>
#include <string>
#include <vector>

struct Test {
    int a;
    std::string name;
};

int main() {
    std::vector<const Test *> x;

    Test test{1, "hello"};

    x.push_back(&test);

    return 0;
}