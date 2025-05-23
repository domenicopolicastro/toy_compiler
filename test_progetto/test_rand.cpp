#include <iostream>
extern "C" double randinit(double);
extern "C" double randk();
extern "C" double floor(double);
int main(){
  randinit(1234.0);
  std::cout << "seed dopo randinit: " 
            << randinit(1234.0) << "\n";
  std::cout << "una chiamata a randk: " 
            << randk() << "\n";
  return 0;
}
