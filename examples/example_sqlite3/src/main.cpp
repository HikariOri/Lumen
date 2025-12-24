#include <iostream>

#include <sqlite_orm/sqlite_orm.h>

// using namespace sqlite_orm;

struct Employee {
    int id;
    std::string name;
    int age;
    std::unique_ptr<std::string> address; //  optional
    std::unique_ptr<double> salary;       //  optional
};

void all_employees() {

    auto storage = sqlite_orm::make_storage(
        "select.sqlite",
        sqlite_orm::make_table(
            "COMPANY",
            sqlite_orm::make_column("ID", &Employee::id,
                                    sqlite_orm::primary_key()),
            sqlite_orm::make_column("NAME", &Employee::name),
            sqlite_orm::make_column("AGE", &Employee::age),
            sqlite_orm::make_column("ADDRESS", &Employee::address),
            sqlite_orm::make_column("SALARY", &Employee::salary)));
    storage.sync_schema();
    storage.remove_all<
        Employee>(); //  remove all old employees in case they exist in db..

    //  create employees..
    Employee paul { .id = -1,
                    .name = "Paul",
                    .age = 32,
                    .address = std::make_unique<std::string>("California"),
                    .salary = std::make_unique<double>(20000.0) };
    Employee allen { .id = -1,
                     .name = "Allen",
                     .age = 25,
                     .address = std::make_unique<std::string>("Texas"),
                     .salary = std::make_unique<double>(15000.0) };
    Employee teddy { .id = -1,
                     .name = "Teddy",
                     .age = 23,
                     .address = std::make_unique<std::string>("Norway"),
                     .salary = std::make_unique<double>(20000.0) };
    Employee mark { .id = -1,
                    .name = "Mark",
                    .age = 25,
                    .address = std::make_unique<std::string>("Rich-Mond"),
                    .salary = std::make_unique<double>(65000.0) };
    Employee david { .id = -1,
                     .name = "David",
                     .age = 27,
                     .address = std::make_unique<std::string>("Texas"),
                     .salary = std::make_unique<double>(85000.0) };
    Employee kim { .id = -1,
                   .name = "Kim",
                   .age = 22,
                   .address = std::make_unique<std::string>("South-Hall"),
                   .salary = std::make_unique<double>(45000.0) };
    Employee james { .id = -1,
                     .name = "James",
                     .age = 24,
                     .address = std::make_unique<std::string>("Houston"),
                     .salary = std::make_unique<double>(10000.0) };

    //  insert employees. `insert` function returns id of inserted object..
    paul.id = storage.insert(paul);
    allen.id = storage.insert(allen);
    teddy.id = storage.insert(teddy);
    mark.id = storage.insert(mark);
    david.id = storage.insert(david);
    kim.id = storage.insert(kim);
    james.id = storage.insert(james);

    //  print users..
    std::cout << "paul = " << storage.dump(paul) << '\n';
    std::cout << "allen = " << storage.dump(allen) << '\n';
    std::cout << "teddy = " << storage.dump(teddy) << '\n';
    std::cout << "mark = " << storage.dump(mark) << '\n';
    std::cout << "david = " << storage.dump(david) << '\n';
    std::cout << "kim = " << storage.dump(kim) << '\n';
    std::cout << "james = " << storage.dump(james) << '\n';

    //  select all employees..
    auto allEmployees = storage.get_all<Employee>();

    std::cout << "allEmployees[0] = " << storage.dump(allEmployees[0]) << '\n';
    std::cout << "allEmployees count = " << allEmployees.size() << '\n';

    //  now let's select id, name and salary..
    auto idsNamesSalarys = storage.select(
        sqlite_orm::columns(&Employee::id, &Employee::name, &Employee::salary));
    for (
        auto &row :
        idsNamesSalarys) { //  row's type is `tuple<int, string, unique_ptr<double>>`
        std::cout << "id = " << get<0>(row) << ", name = " << get<1>(row)
                  << ", salary = ";
        if (get<2>(row)) {
            std::cout << *get<2>(row);
        } else {
            std::cout << "null";
        }
        std::cout << '\n';
    }

    std::cout << '\n';

    auto allEmployeeTuples = storage.select(sqlite_orm::asterisk<Employee>());
    std::cout << "allEmployeeTuples count = " << allEmployeeTuples.size()
              << '\n';
    for (
        auto &row :
        allEmployeeTuples) { //  row's type is std::tuple<int, string, int, std::unique_ptr<string>,
        //  std::unique_ptr<double>>
        std::cout << get<0>(row) << '\t' << get<1>(row) << '\t' << get<2>(row)
                  << '\t';
        if (auto &value = get<3>(row)) {
            std::cout << *value;
        } else {
            std::cout << "null";
        }
        std::cout << '\t';
        if (auto &value = get<4>(row)) {
            std::cout << *value;
        } else {
            std::cout << "null";
        }
        std::cout << '\t' << '\n';
    }

    std::cout << '\n';

    auto allEmployeeObjects = storage.select(sqlite_orm::object<Employee>());
    std::cout << "allEmployeeObjects count = " << allEmployeeObjects.size()
              << '\n';
    for (auto &employee : allEmployeeObjects) {
        std::cout << employee.id << '\t' << employee.name << '\t'
                  << employee.age << '\t';
        if (auto &value = employee.address) {
            std::cout << *value;
        } else {
            std::cout << "null";
        }
        std::cout << '\t';
        if (auto &value = employee.salary) {
            std::cout << *value;
        } else {
            std::cout << "null";
        }
        std::cout << '\t' << '\n';
    }

    std::cout << '\n';
}

void all_artists() {
    struct Artist {
        int id;
        std::string name;
    };

    struct Album {
        int id;
        int artist_id;
    };

    auto storage = sqlite_orm::make_storage(
        "",
        sqlite_orm::make_table(
            "artists",
            sqlite_orm::make_column("id", &Artist::id,
                                    sqlite_orm::primary_key().autoincrement()),
            sqlite_orm::make_column("name", &Artist::name)),
        sqlite_orm::make_table(
            "albums",
            sqlite_orm::make_column("id", &Album::id,
                                    sqlite_orm::primary_key().autoincrement()),
            sqlite_orm::make_column("artist_id", &Album::artist_id),
            sqlite_orm::foreign_key(&Album::artist_id)
                .references(&Artist::id)));
    storage.sync_schema();
    storage.transaction([&storage] {
        auto artistPk = storage.insert(Artist { -1, "Artist" });
        storage.insert(Album { -1, artistPk });
        storage.insert(Album { -1, artistPk });
        return true;
    });

    // SELECT artists.*, albums.* FROM artists JOIN albums ON albums.artist_id = artist.id

    std::cout << "artists.*, albums.*\n";
    // row's type is `std::tuple<int, std::string, id, int>`
    for (auto &row : storage.select(
             sqlite_orm::columns(sqlite_orm::asterisk<Artist>(),
                                 sqlite_orm::asterisk<Album>()),
             sqlite_orm::join<Album>(sqlite_orm::on(
                 sqlite_orm::c(&Album::artist_id) == &Artist::id)))) {
        std::cout << get<0>(row) << '\t' << get<1>(row) << '\t' << get<2>(row)
                  << '\t' << get<3>(row) << '\n';
    }
    std::cout << '\n';
}

void named_adhoc_structs() {
    struct Artist {
        int id;
        std::string name;
    };

    struct Album {
        int id;
        int artist_id;
        std::string name;
    };

    struct Z {
        decltype(Album::name) album_name;
        decltype(Artist::name) artist_name;
    };
    // define SQL expression for ad-hoc construction of Z
    constexpr auto z_struct =
        sqlite_orm::struct_<Z>(&Album::name, &Artist::name);

    auto storage = sqlite_orm::make_storage(
        "",
        sqlite_orm::make_table(
            "artists",
            sqlite_orm::make_column("id", &Artist::id,
                                    sqlite_orm::primary_key().autoincrement()),
            sqlite_orm::make_column("name", &Artist::name)),
        sqlite_orm::make_table(
            "albums",
            sqlite_orm::make_column("id", &Album::id,
                                    sqlite_orm::primary_key().autoincrement()),
            sqlite_orm::make_column("artist_id", &Album::artist_id),
            sqlite_orm::make_column("name", &Album::name),
            sqlite_orm::foreign_key(&Album::artist_id)
                .references(&Artist::id)));
    storage.sync_schema();
    storage.transaction([&storage] {
        auto artistPk = storage.insert(Artist { -1, "Artist" });
        storage.insert(Album { -1, artistPk, "Album 1" });
        storage.insert(Album { -1, artistPk, "Album 2" });
        return true;
    });

    // SELECT albums.name, artists.name FROM albums JOIN artists ON artist.id = albums.artist_id

    std::cout << "albums.name, artists.name\n";
    // row's type is Z
    for (auto &row : storage.select(
             z_struct, sqlite_orm::join<Album>(sqlite_orm::on(
                           sqlite_orm::c(&Album::artist_id) == &Artist::id)))) {
        std::cout << row.album_name << '\t' << row.artist_name << '\n';
    }
    std::cout << '\n';
}

int main() {
    try {
        all_employees();
        all_artists();
        named_adhoc_structs();
    } catch (const std::system_error &e) {
        std::cout << "[" << e.code() << "] " << e.what();
    }

    return 0;
}
