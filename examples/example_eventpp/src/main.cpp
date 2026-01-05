#include <print>

#include <eventpp/callbacklist.h>
#include <eventpp/eventdispatcher.h>
#include <eventpp/eventqueue.h>

int main() {

    {
        eventpp::CallbackList<void(const std::string &, const bool)>
            callbackList;
        callbackList.append([](const std::string &s, const bool b) {
            std::println("Got callback 1, s is {} b is {}", s, b);
        });
        callbackList.append([](std::string s, int b) {
            std::println("Got callback 2, s is {} b is {}", s, b);
        });
        callbackList("Hello world", true);
    }

    std::println();

    {
        eventpp::EventDispatcher<int, void()> dispatcher;
        dispatcher.appendListener(3, []() { std::println("Got event 3."); });
        dispatcher.appendListener(5, []() { std::println("Got event 5."); });
        dispatcher.appendListener(
            5, []() { std::println("Got another event 5."); });
        // dispatch event 3
        dispatcher.dispatch(3);
        // dispatch event 5
        dispatcher.dispatch(5);
    }

    std::println();

    {
        eventpp::EventQueue<int, void(const std::string &, const bool)> queue;

        queue.appendListener(3, [](const std::string &s, bool b) {
            std::println("Got event 3, s is {} b is {}", s, b);
        });
        queue.appendListener(5, [](const std::string &s, bool b) {
            std::println("Got event 5, s is {} b is {}", s, b);
        });

        // The listeners are not triggered during enqueue.
        queue.enqueue(3, "Hello", true);
        queue.enqueue(5, "World", false);

        // Process the event queue, dispatch all queued events.
        queue.process();
    }

    return 0;
}
