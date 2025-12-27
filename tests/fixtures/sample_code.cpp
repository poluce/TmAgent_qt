// Sample C++ code for testing CodeParserTool

#include <iostream>

namespace TestNamespace {

class Calculator {
public:
    Calculator() {}
    
    int add(int a, int b) {
        return a + b;
    }
    
    int subtract(int a, int b) {
        return a - b;
    }
};

} // namespace TestNamespace

int main() {
    TestNamespace::Calculator calc;
    std::cout << calc.add(1, 2) << std::endl;
    return 0;
}

void helperFunction(const std::string& msg) {
    std::cout << msg << std::endl;
}
