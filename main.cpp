#include <iostream>
#include <vector>
#include <fcntl.h>
#include <aio.h>
#include <unistd.h>
#include <csignal>
#include <cstring>
#include <sys/stat.h>
#include <atomic>

constexpr size_t CHUNK_SIZE = 4096;
constexpr int MAX_AIO = 4;

struct AsyncTask {
    aiocb cb{};
    char* buffer;
    bool is_write;
    int out_fd;
};

std::atomic<int> active_tasks{0};

void handle_aio_complete(sigval_t sigval) {
    AsyncTask* task = static_cast<AsyncTask*>(sigval.sival_ptr);

    if (!task->is_write) {
        ssize_t bytes_read = aio_return(&task->cb);
        if (bytes_read <= 0) {
            if (bytes_read < 0) perror("Read error");
            delete[] task->buffer;
            delete task;
            active_tasks--;
            return;
        }

        AsyncTask* write_task = new AsyncTask;
        write_task->buffer = task->buffer;
        write_task->is_write = true;
        write_task->out_fd = task->out_fd;

        write_task->cb = {};
        write_task->cb.aio_fildes = task->out_fd;
        write_task->cb.aio_buf = write_task->buffer;
        write_task->cb.aio_nbytes = bytes_read;
        write_task->cb.aio_offset = task->cb.aio_offset;

        write_task->cb.aio_sigevent.sigev_notify = SIGEV_THREAD;
        write_task->cb.aio_sigevent.sigev_notify_function = handle_aio_complete;
        write_task->cb.aio_sigevent.sigev_notify_attributes = nullptr;
        write_task->cb.aio_sigevent.sigev_value.sival_ptr = write_task;

        if (aio_write(&write_task->cb) < 0) {
            perror("Write error");
            delete[] write_task->buffer;
            delete write_task;
        }

        delete task;
    } else {
        aio_return(&task->cb);
        delete[] task->buffer;
        delete task;
        active_tasks--;
    }
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <source> <destination>\n";
        return 1;
    }

    const char* src_path = argv[1];
    const char* dst_path = argv[2];

    int src = open(src_path, O_RDONLY);
    if (src < 0) {
        perror("Failed to open source");
        return 1;
    }

    int dst = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst < 0) {
        perror("Failed to open destination");
        close(src);
        return 1;
    }

    struct stat src_stat{};
    if (fstat(src, &src_stat) != 0) {
        perror("fstat");
        close(src);
        close(dst);
        return 1;
    }

    off_t offset = 0;
    off_t total_size = src_stat.st_size;

    while (offset < total_size) {
        while (active_tasks >= MAX_AIO) {
            usleep(1000);
        }

        size_t bytes_to_read = std::min(static_cast<off_t>(CHUNK_SIZE), total_size - offset);

        AsyncTask* read_task = new AsyncTask;
        read_task->buffer = new char[bytes_to_read];
        read_task->is_write = false;
        read_task->out_fd = dst;

        read_task->cb = {};
        read_task->cb.aio_fildes = src;
        read_task->cb.aio_buf = read_task->buffer;
        read_task->cb.aio_nbytes = bytes_to_read;
        read_task->cb.aio_offset = offset;

        read_task->cb.aio_sigevent.sigev_notify = SIGEV_THREAD;
        read_task->cb.aio_sigevent.sigev_notify_function = handle_aio_complete;
        read_task->cb.aio_sigevent.sigev_notify_attributes = nullptr;
        read_task->cb.aio_sigevent.sigev_value.sival_ptr = read_task;

        if (aio_read(&read_task->cb) < 0) {
            perror("Read start failed");
            delete[] read_task->buffer;
            delete read_task;
            break;
        }

        active_tasks++;
        offset += bytes_to_read;
    }

    while (active_tasks > 0) {
        usleep(1000);
    }

    close(src);
    close(dst);
    std::cout << "File copied asynchronously.\n";
    return 0;
}
