#include <drogon/drogon_test.h>

#include "core/api_error.h"
#include "utils/validation.h"

using Params = std::unordered_map<std::string, std::string>;

DROGON_TEST(QueryDefaults) {
    auto q = validation::parseTodoQuery(Params{});
    CHECK(q.page == 1);
    CHECK(q.pageSize == 20);
    CHECK(!q.cursorMode);
    REQUIRE(q.sort.size() == 1);
    CHECK(q.sort[0].column == "created_at");
    CHECK(q.sort[0].desc);
}

DROGON_TEST(QueryFilters) {
    Params p{{"status", "todo,done,todo"}, {"priority_min", "2"}, {"tags", "Work,home"}, {"sort", "-priority,due_date"}, {"q", "milk"}, {"overdue", "true"}};
    auto q = validation::parseTodoQuery(p);
    CHECK(q.filter.statuses.size() == 2);
    CHECK(*q.filter.priorityMin == 2);
    CHECK(q.filter.tags.size() == 2);
    CHECK(q.filter.tags[0] == "work");
    CHECK(q.sort.size() == 2);
    CHECK(*q.filter.overdue);
}

DROGON_TEST(QueryRejectsBadInput) {
    Params bad1{{"status", "bogus"}};
    CHECK_THROWS_AS(validation::parseTodoQuery(bad1), ApiError);
    Params bad2{{"page_size", "1000"}};
    CHECK_THROWS_AS(validation::parseTodoQuery(bad2), ApiError);
    Params bad3{{"sort", "password"}};
    CHECK_THROWS_AS(validation::parseTodoQuery(bad3), ApiError);
    Params bad4{{"pagination", "cursor"}, {"sort", "title"}};
    CHECK_THROWS_AS(validation::parseTodoQuery(bad4), ApiError);
}

DROGON_TEST(CursorRoundTrip) {
    auto c = validation::encodeCursor(12345);
    auto id = validation::decodeCursor(c);
    REQUIRE(id.has_value());
    CHECK(*id == 12345);
    CHECK(!validation::decodeCursor("garbage").has_value());
}

DROGON_TEST(NewTodoNormalisesTags) {
    Json::Value b;
    b["title"] = "  Buy milk ";
    b["tags"].append("Home");
    b["tags"].append("home");
    auto t = validation::parseNewTodo(b);
    CHECK(t.title == "Buy milk");
    CHECK(t.tags.size() == 1);
    CHECK(t.priority == 2);
}

DROGON_TEST(PatchSupportsExplicitNull) {
    Json::Value b;
    b["description"] = Json::nullValue;
    auto p = validation::parseTodoPatch(b);
    CHECK(p.description.set);
    CHECK(!p.description.value.has_value());
    CHECK(!p.title.set);
    Json::Value empty(Json::objectValue);
    CHECK_THROWS_AS(validation::parseTodoPatch(empty), ApiError);
}

int main(int argc, char** argv) { return drogon::test::run(argc, argv); }
