#include <future>
#include <memory>
#include <print>
#include <thread>

#include <efsw/efsw.hpp>
#include <ghc/filesystem.hpp>

// Inherits from the abstract listener class, and implements the the file action
// handler
class UpdateListener : public efsw::FileWatchListener {
public:
    void handleFileAction(efsw::WatchID watchid, const std::string &dir,
                          const std::string &filename, efsw::Action action,
                          std::string oldFilename) override {

        ghc::filesystem::path path = dir;
        path /= filename;

        switch (action) {
        case efsw::Actions::Add:
            if (ghc::filesystem::is_regular_file(path)) {
                std::println("DIR ({}) FILE ({}) has event Added", dir,
                             filename);
            }
            if (ghc::filesystem::is_directory(path)) {
                std::println("DIR ({}) DIR ({}) has event Added", dir,
                             filename);
            }
            break;
        case efsw::Actions::Delete:
            std::println("DIR ({}) FILE ({}) has event Delete", dir, filename);
            break;
        case efsw::Actions::Modified:
            std::println("DIR ({}) FILE ({}) has event Modified", dir,
                         filename);
            break;
        case efsw::Actions::Moved:
            std::println("DIR ({}) FILE ({}) has event Moved from ({})", dir,
                         filename, oldFilename);
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
