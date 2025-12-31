#include <future>
#include <memory>
#include <print>
#include <thread>

#include <efsw/efsw.hpp>
#include <ghc/filesystem.hpp>

namespace fs = ghc::filesystem;

// Inherits from the abstract listener class, and implements the the file action
// handler
class UpdateListener : public efsw::FileWatchListener {
public:
    void handleFileAction(efsw::WatchID watchid, const std::string &dir,
                          const std::string &filename, efsw::Action action,
                          std::string oldFilename) override {

        fs::path path = dir;
        path /= filename;

        std::string fileOrDir = fs::is_regular_file(path) ? "FILE" : "DIR";

        switch (action) {
        case efsw::Actions::Add:
            std::println("DIR ({}) {} ({}) has event Added", dir, fileOrDir,
                         filename);
            break;
        case efsw::Actions::Delete:
            std::println("DIR ({}) {} ({}) has event Delete", dir, fileOrDir,
                         filename);
            break;
        case efsw::Actions::Modified:
            std::println("DIR ({}) {} ({}) has event Modified", dir, fileOrDir,
                         filename);
            break;
        case efsw::Actions::Moved:
            std::println("DIR ({}) {} ({}) has event Moved from ({})", dir,
                         fileOrDir, filename, oldFilename);
            break;
        default: std::println("Should never happen!");
        }
    }
};

int main(int argc, char *argv[]) {

    auto fileWatcher = std::make_shared<efsw::FileWatcher>();

    // Create the instance of your efsw::FileWatcherListener implementation
    auto listener = std::make_unique<UpdateListener>();

    efsw::WatchID watchID =
        fileWatcher->addWatch("D:/test_water", listener.get(), true);

    auto result = std::async(std::launch::async, [&fileWatcher]() {
        std::println("async thread: {}", std::this_thread::get_id());
        while (true) {
            fileWatcher->watch();
        }
    });

    std::println("main thread: {}", std::this_thread::get_id());

    result.get();

    fileWatcher->removeWatch(watchID);

    return 0;
}
