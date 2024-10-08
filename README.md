<!-- # clusterChatServer -->
# 基于muduo网络库的集群聊天服务器

<!-- 基于nginx tcp负载均衡、redis消息发布-订阅模式和muduo网络库的集群聊天服务器和客户端源码 -->

## 项目概述
项目大概是模仿QQ等通讯软件去实现一个通讯工具，主要业务分为注册、登录、加好友、查看离线消息、一对一群聊、创建群、加入群、群聊等，详细业务流程关系如下图：
![alt text](./pics/image.png)

作为一个集群系统需要考虑的问题:
* 如何解耦网络层与业务层？
* 要存储那些数据？怎么存储？
* 客户端和服务器端要求的通信格式是什么？
* 如何做到集群？
* 既然是集群，跨服务器通信又该怎么解决？


## 文件架构及使用方式
项目代码文件目录结构：
|--bin      存放可执行文件
|--build    cmake构建的中间文件
|--example  存放框架使用示例代码
|--include  存放头文件
|--lib      编译生成的动态库
|--src      项目源代码  存放框架代码
|--test     示例代码
|--autobuild.sh  一键编译
|--CMakeLists.txt  Cmake顶层目录配置文件

使用方式：用户可以通过运行autobuild.sh脚本一键编译项目文件，也可以通过如下步骤编译项目：
1. 进入buil目录
2. 执行`cmake ..`
3. 执行`make`
4. 如果没问题的话，会在bin目录生成ChatClient和ChatServer可执行文件



## 数据模块
### 表的设计
首先就是要解决数据的问题，作为一个聊天系统，服务器端肯定要有用户的信息，比如说账号，用户名，密码等。在登录的时候，可以查询表中的信息对用户身份进行验证，在注册的时候，则可以往表中写入数据。
表user：
| 字段名称 | 字段类型                  | 字段说明     | 约束                        |
| -------- | ------------------------- | ------------ | --------------------------- |
| id       | INT                       | 用户id       | PRIMARY KEY、AUTO_INCREMENT |
| name     | VARCHAR(50)               | 用户名       | NOT NULL，UNIQUE            |
| password | VARCHAR(50)               | 用户密码     | NOT NULL                    |
| state    | ENUM('online', 'offline') | 当前登录状态 | DEFAULT 'offline'           |

用户登录之后，首先就是进行聊天业务，因此必须要知道该用户的好友都有谁。在加好友时，可以往好友表中写入信息并在一对一聊天时查询这里面的信息去看好友是否在线。
表friend：
| 字段名称 | 字段类型 | 字段说明 | 约束                |
| -------- | -------- | -------- | ------------------- |
| userid   | INT      | 用户id   | NOT NULL、 联合主键 |
| friendid | INT      | 好友id   | NOT NULL、联合主键  |

但是我们的设计目的又要存储离线消息，这就涉及到离线消息发给谁，谁发的，发的什么三个问题，所以又需要一个新表来存储离线消息。
表offlinemessage：
| 字段名称 | 字段类型     | 字段说明                 | 约束     |
| -------- | ------------ | ------------------------ | -------- |
| userid   | INT          | 用户id                   | NOT NULL |
| message  | VARCHAR(500) | 离线消息(存储Json字符串) | NOT NULL |

然后便是群组业务了，群组中我们需要有一个记录群组信息的表，方便我们创建群时往其中去写入数据。
表allgroup:
| 字段名称  | 字段类型     | 字段说明     | 约束                        |
| --------- | ------------ | ------------ | --------------------------- |
| id        | INT          | 群组id       | PRIMARY KEY、AUTO_INCREMENT |
| groupname | VARCHAR(50)  | 群组名称     | NOT NULL, UNIQUE            |
| groupdesc | VARCHAR(200) | 群组信息描述 | DEFAULT "                   |

同时群里面肯定是有群员的，我们就需要一个记录群成员的表，我们在加入群的时候，把用户id写入这个表。并且在发送群消息的时候查询这个表，由服务器向这些成员转发消息。
表groupuser:
| 字段名称  | 字段类型                  | 字段说明 | 约束               |
| --------- | ------------------------- | -------- | ------------------ |
| groupid   | INT                       | 组id     | NOT NULL、联合主键 |
| userid    | INT                       | 组员id   | NOT NULL、联合主键 |
| grouprole | ENUM('creator'，'normal') | 组内角色 | DEFAULT 'normal'   |

### 数据库模块设计
在/include/db/MySQL.hpp文件中，封装着对数据库的连接、查询、更新、释放连接几个操作。
数据库模块在最底层，为上层各个表及其操作模块提供基础的服务。其关系图如下所示：
![alt text](./pics/image-1.png)

* 名字带有model的类都是对数据库的操作类并不负责存储数据，而像User这个类则是负责暂存从数据库中查询到的数据
* FriendModel和OfflineMessageModel则是没有暂存数据的上层类，这是因为对于Friend来说，其数据本身就是一个User，只需要查询到好友的id然后在User表中内联查询一下便可得到信息；对于OfflineMessage这是没有必要
* 这些类都在/include/model里面

## 通信格式
服务器和客户端的通信采用了JSON来完成数据在网络中的标准传输。
对于不同的数据则是采用了不同的格式，具体如下：
```json
1.登录
json["msgid"] = LOGIN_MSG;
json["id"]			//用户id
json["password"]	//密码

2.登录反馈
json["msgid"] = LOGIN_MSG_ACK;
json["id"]			//登录用户id
json["name"]		//登录用户密码
json["offlinemsg"]	//离线消息
json["friends"]		//好友信息,里面有id、name、state三个字段
json["groups"]		//群组信息,里面有id，groupname，groupdesc，users三个字段
					//users里面则有id，name，state，role四个字段
json["errno"]		//错误字段，错误时被设置成1，用户不在线设置成2
json["errmsg"]		//错误信息

3.注册
json["msgid"] = REG_MSG;
json["name"]		//用户姓名
json["password"]	//用户姓名

4.注册反馈
json["msgid"] = REG_MSG_ACK;
json["id"]			//给用户返回他的id号
json["errno"]		//错误信息，失败会被设置为1

5.加好友
json["msgid"] = ADD_FRIEND_MSG;
json["id"]			//当前用户id
json["friendid"]	//要加的好友的id

6.一对一聊天
json["msgid"] = ONE_CHAT_MSG;
json["id"]			//发送者id
json["name"]		//发送者姓名
json["to"]			//接受者id
json["msg"]			//消息内容
json["time"]		//发送时间

7.创建群
json["msgid"] = CREATE_GROUP_MSG;
json["id"]			//群创建者id
json["groupname"]	//群名
json["groupdesc"]	//群描述

8.加入群
json["msgid"] = ADD_GROUP_MSG;
json["id"]			//用户id
json["groupid"]		//群id

9.群聊
json["msgid"] = GROUP_CHAT_MSG;
json["id"]			//发送者id
json["name"]		//发送者姓名
json["groupid"]		//发送者姓名
json["msg"]			//消息内容
json["time"]		//发送时间

10.注销
json["msgid"] = LOGINOUT_MSG;
json["id"]			//要注销的id
```
msgid字段代表着业务类型，这个在/include/public.hpp文件中有详细解释。
这里并没有User表中的state字段，这是因为，这个表示用户是否在线的字段会被服务器自动设置，当用户登录会被设置成online，用户下线或者服务器异常则会被设置成offline

## 网络和业务模块
### 网络模块
在这里网络模块并没有自己去socket+epoll这样造轮子，而是选择直接使用muduo网络库提供的接口。
使用muduo网络库有很多好处：
1. 方便，简单
2. one loop per thread的设计模型
3. muduo封装了线程池

>什么是one loop per thread？
这是muduo网络库采用的reactor模型，有点像Nginx的负载均衡，但是也有差别，Nginx采用的是多进程，而muduo是多线程。
在muduo设计中，有一个main reactor负责接收来自客户端的连接。然后使用轮询的方式给sub reactor去分配连接，而客户端的读写事件都在这个sub reactor上进行。

muduo网络库提供了两个非常重要的注册回调接口：连接回调和消息回调
```cpp {.line-numbers}
//注册连接回调
server_.setConnectionCallback(bind(&ChatServer::onConnection, this, _1));

//注册消息回调
server_.setMessageCallback(bind(&ChatServer::onMessage, this, _1, _2, _3));
```
设置处理有关连接事件和处理读写事件的回调方法：
```cpp {.line-numbers}
//上报连接相关信息的回调函数
void onConnection(const TcpConnectionPtr &);

//上报读写时间相关信息的回调函数
void onMessage(const TcpConnectionPtr &, Buffer *, Timestamp);
```
当用户进行连接或者断开连接时便会调用onConnection方法进行处理，其执行对象应该是main reactor。
发生读写事件时，则会调用onMessage方法，执行对象为sub reactor，其内容与网络模块和业务模块解耦。

#### 网络模块和业务模块解耦合
在通信模块中，有一个字段msgid，其代表着服务器和客户端通信的消息类型，值是枚举类型，保存在/include/public.hpp文件中，总共有10个取值：
```cpp 
LOGIN_MSG = 1,    //登录消息
LOGIN_MSG_ACK,    //登录响应消息
REG_MSG,          //注册消息
REG_MSG_ACK,      //注册响应消息
ONE_CHAT_MSG,     //一对一聊天消息
ADD_FRIEND_MSG,   //添加好友消息

CREATE_GROUP_MSG, //创建群聊
ADD_GROUP_MSG,    //加入群聊
GROUP_CHAT_MSG,   //群聊消息

LOGINOUT_MSG,     //注销消息
```
根据这些消息类型，我们可以在业务模块添加一个无序关联容器unordered_map，其键为消息类型，值为发生不同类型事件所应该调用的方法。
```cpp {.line-numbers}
msg_handler_map_.insert({LOGIN_MSG, bind(&ChatService::login, this, _1, _2, _3)});
msg_handler_map_.insert({LOGINOUT_MSG, bind(&ChatService::loginout, this, _1, _2, _3)});
msg_handler_map_.insert({REG_MSG, bind(&ChatService::reg, this, _1, _2, _3)});
msg_handler_map_.insert({ONE_CHAT_MSG, bind(&ChatService::one_chat, this, _1, _2, _3)});
msg_handler_map_.insert({ADD_FRIEND_MSG, bind(&ChatService::add_friend, this, _1, _2, _3)});
msg_handler_map_.insert({CREATE_GROUP_MSG, bind(&ChatService::create_group, this, _1, _2, _3)});
msg_handler_map_.insert({ADD_GROUP_MSG, bind(&ChatService::add_group, this, _1, _2, _3)});
msg_handler_map_.insert({GROUP_CHAT_MSG, bind(&ChatService::group_chat, this, _1, _2, _3)});
```

在网络层可以根据消息类型来获得并执行其handler：
```cpp {.line-numbers}
//解耦网络和业务模块的代码
//通过js里面的msgid，绑定msgid的回调函数，获取业务处理器handler
auto msg_handler = ChatService::instance()->get_handler(js["msgid"].get<int>());

msg_handler(conn, js, time);
```

### 业务模块
由于与网络模块的解耦，在业务模块我们就不用去关系网络上所发生的事情，专注业务便可。

具体业务相关的数据结构如下：
```cpp {.line-numbers}
//存储在线用户的连接情况，便于服务器给用户发消息，注意线程安全
unordered_map<int, TcpConnectionPtr> _userConnMap;

mutex _connMutex;

UserModel _userModel;
OfflineMessageModel _offlineMsgModel;
FriendModel _friendModel;
GroupModel _groupModel;
```

#### 注册业务
当服务器接收到json字符串后，对其进行反序列化，得到要注册的信息，然后写入到User表中，成功就将id号返回，失败就把errno字段设置为1。

#### 登录业务
当服务器接收到json字符串后，对其进行反序列化，得到用户传递过来的账号和密码信息。
首先检查这个账号和密码是否与服务器中的数据匹配，如果不匹配就把errno设置为1并返回id or password error的错误信息。
如果匹配，就检查当前用户是否在线，如果在线，可能是该账号在别的设备登录的状况，此时就把errno设置为2，返回id is online的错误信息
如果用户不在线，这个时候用户就是登陆成功了，这个时候服务器就把该用户的好友列表，群组列表以及离线消息都推送给该用户。

#### 加好友业务
当服务器接收到json字符串后，对其进行反序列化，服务器得到反序列化的信息，然后将这个信息写入Friend表中即可。

#### 一对一聊天业务
服务器接收到客户端的信息，然后去本服务器的_userConnMap接受信息的用户是否在本服务器在线，在线的话直接转发即可，不在线的话，看看数据库里面的信息是否是在线，如果在线，那么就是接收用户在其他服务器登录，将消息通过redis中间件转发即可。
如果均不在线，转储离线消息即可。

#### 创建群业务
服务器接收到客户端的信息，把群组的信息写入到allgroup表中，并将创建者的信息写入到groupuser中，设置创建者为creator

#### 加入群业务
服务器接收到客户端的信息，将用户数据写入到groupuser表中，并将role角色设置为normal。

#### 群聊业务
服务器接收到客户端的信息，先去groupuser表查询到所有群员的id，然后一个个去本服务器的_userConnMap查看接受信息的用户是否在本服务器在线，在线的话直接转发即可，不在线的话，看看数据库里面的信息是否是在线，如果在线，那么就是接收用户在其他服务器登录，将消息通过redis中间件转发即可。
如果均不在线，转储离线消息即可。

#### 注销业务
服务器收到客户端发来的信息，将该用户在user表中所对应的state改为offline。


## 服务器集群
一般来说，一台服务器只能支持1~2w的并发量，但是这是远远不够的，我们需要更高的并发量支持，这个时候就需要引入Nginx tcp长连接负载均衡算法。

当一个新的客户端连接到来时，负载均衡模块便会根据我们在nginx.conf里面设置的参数来分配服务器资源。
![alt text](./pics/image-2.png)
按图中所示，客户端只用连接我们的负载均衡服务器，然后nginx服务器就会自动把client连接分配到对应的server服务器。


## 跨服务器通信
如果我一个在server1的用户想要给在server2的用户发送消息要怎么办呢？
是像下面这样把每个服务器连接起来么？
![alt text](./pics/image-3.png)
这样肯定不行，服务器之间关联性太强了，一旦多加一个服务器，以前的服务器都要增加一条指向它的连接。
所以，我们可以借鉴交换机连接PC的思想，引入Redis消息队列中间件！
![alt text](./pics/image-4.png)

当客户端登录的时候，服务器把它的id号subscribe到redis中间件，表示该服务器对这个id发生的事件感兴趣，而Redis收到发送给该id的消息时就会把消息转发到这个服务器上。


## 集群聊天服务器的思考
服务器设计上还是存在一些问题的：
* 如果服务器宕机，正在连接该服务的用户数据怎么办？
* 服务器缺少一个文件服务器来提供数据模块所需的信息
* 服务器和文件服务器是直接连接查询还是服务器存储一部分信息然后隔段时间同步一下
* 加好友的时候应该像QQ那样发送请求，可以设置是否需要同意后才能添加，如果设置了则需要等待对方接受再写入

