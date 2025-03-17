#include <iostream>
#include <fstream>
#include <vector>
#include <future>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <cstring>

#define MAX_OPERATIONS 12

struct FileOperation {
    std::vector<char> buffer;
    size_t offset;
    std::string input_filename;
    std::string output_filename;
};

void async_read_write(FileOperation op, size_t file_size) {
    
    std::ifstream input_file(op.input_filename, std::ios::binary);

    size_t bytes_to_read = std::min(op.buffer.size(), file_size - op.offset);
    op.buffer.resize(bytes_to_read);  

    input_file.seekg(op.offset);

    input_file.read(op.buffer.data(), bytes_to_read);
    size_t bytes_read = input_file.gcount();
    input_file.close();

    std::ofstream output_file(op.output_filename, std::ios::binary | std::ios::app);

    output_file.seekp(op.offset);
    output_file.write(op.buffer.data(), bytes_read);
    output_file.close();
    
}

int main() {

    std::string fileName = "s.txt";
    std::string fileNameS = "d.txt";

    for (int j = 1024; j <= 6144; j+=1024)
    {
        std::cout << j << std::endl;
        struct stat file_stat;
        size_t file_size = file_stat.st_size;

        struct statfs fs_stat;
        size_t cluster_size = j; //= std::min<size_t>(fs_stat.f_bsize, file_size);

        std::ofstream test_write(fileNameS, std::ios::binary | std::ios::trunc);
        test_write.close();

        std::vector<std::future<void>> tasks;

        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < 1; i++) {
            FileOperation op;
            op.buffer.resize(cluster_size);
            op.offset = i * cluster_size;
            op.input_filename = fileName;
            op.output_filename = fileNameS;

            tasks.push_back(std::async(std::launch::async, async_read_write, op, file_size));
        }

        auto end = std::chrono::high_resolution_clock::now();
        std::cout << "Copy completed in: " << std::chrono::duration<double>(end - start).count() << " seconds\n";

        //system("diff so.txt de.txt");
        system("rm d.txt");
        for (auto &task : tasks) {
            task.get();
        }
    }

    return 0;
}
