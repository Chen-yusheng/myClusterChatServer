#ifndef USER_H
#define USER_H

#include <string>
using namespace std;

// user表的ORM类 数据表字段映射类
class User
{
public:
    User(int id = -1, string name = "", string password = "", string state = "offline")
        : id(id), name(name), password(password), state(state)
    {
    }

    void setId(int id) { this->id = id; }
    void setName(string name) { this->name = name; }
    void setPassword(string password) { this->password = password; }
    void setState(string state) { this->state = state; }

    int getId() const { return id; }
    string getName() const { return name; }
    string getPassword() const { return password; }
    string getState() const { return state; }

private:
    int id;
    string name;
    string password;
    string state;
};

#endif