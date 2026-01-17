#include <fstream>

int main(){
    std::fstream file("test.bin",std::ios::in |std::ios::out | std::ios::binary);
    if(!file){
        std::ofstream temp("test.bin",std::ios::binary);
        temp.close();
        std::fstream file("test.bin",std::ios::in |std::ios::out | std::ios::binary);
    }
    file.write("hello",5);
    
    file.seekp(10);
    file.write("world",5);
    
}