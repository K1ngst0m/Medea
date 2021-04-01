#include "pch.h"
#include "NetServer.h"


NetServer::NetServer(IPluginManager *pluginManager)
	: m_pluginManager(pluginManager){}

NetServer::~NetServer() = default;

bool NetServer::Init()
{
    MODULE_INFO("NetService Init");

#ifndef ENABLE_NEW_SOCKET_IMPL
	m_serverSocket = -1;
#else
//    m_serverContext.reset();
//    m_serverSocket.reset();
//    m_serverAcceptor.reset();
#endif

    m_nClientGUID = 0;
	return true;
}

bool NetServer::Update()
{
    MODULE_TRACE("NetService Update");
#ifndef ENABLE_NEW_SOCKET_IMPL
	if (m_serverSocket > 0)
	{
		int  client;
		int nsize = sizeof(sockaddr);
		sockaddr    clientAddr{};

		// 连接客户端
		client = accept(m_serverSocket, &clientAddr, reinterpret_cast<socklen_t *>(&nsize));
		if (client > 0)
		{
		    MODULE_INFO("Client Connected");
			AddClient(client);
		}

		std::list<uint64_t> m_removeClient;

		for (auto kv : m_mapClient)
		{
			char szBuf[255];
			int nLen = recv((int)kv.first, szBuf, 1024, 0);
			bool bError = false;
			if (nLen > 0)
			{
			    MODULE_INFO("Server Receive Data");
				kv.second->m_recvData.append(szBuf, nLen);
			}
			else
			{
			    // TODO : error
//				if (GetLastError() != WSAEWOULDBLOCK)
//				{
//					bError = true;
//				}
			}

			if (!bError)
			{
				if (kv.second->m_sendData.length() > 0)
				{
					int nLen = send((int)kv.first, kv.second->m_sendData.data(), kv.second->m_sendData.length(), 0);
					if (nLen > 0)
					{
						kv.second->m_sendData.erase(0, nLen);
					}
					else
					{
					    // TODO : error
//						if (GetLastError() != WSAEWOULDBLOCK)
//						{
//							bError = true;
//						}
					}
				}
			}

			if(bError)
			{
			    MODULE_ERROR("");
				m_removeClient.push_back(kv.first);
			}
			else
			{
				if (kv.second->m_recvData.length() >= 4)
				{
					const char *pData = kv.second->m_recvData.data();
					size_t nIndex = 0;
					uint16_t nSize = *(uint16_t*)pData;
					uint16_t nType = *(uint16_t*)(pData+sizeof(uint16_t));

					while ((kv.second->m_recvData.length()-nIndex) >= nSize)
					{
						auto itr = m_onReceiveCB.find(nType);
						if (itr != m_onReceiveCB.end())
						{
							for (const auto& onReceive : itr->second)
							{
								(*onReceive)(kv.second->m_guid, nType, kv.second->m_recvData.data() + nIndex, nSize);
							}
						}
						nIndex += nSize;
					}
					if (nIndex != 0)
					{
						kv.second->m_recvData.erase(0, nIndex);
					}
				}
			}
		}

		for (auto removeClient : m_removeClient)
		{
			RemoveClient(removeClient);
		}
	}
	return true;
#else
    if(m_serverSocket->is_open()) {
        // 连接客户端
        auto client = std::make_shared<tcp::socket>(tcp::socket(*m_serverContext));
        m_serverAcceptor->accept(*client);
        if(client->is_open()) {
            MODULE_INFO("client connected.");
            AddClient(client.get());
        }

        std::list<tcp::socket*> m_removeClient;

        // 消息处理
        auto bError = false;
        for (const auto& kv : m_mapClient) {
            MODULE_INFO("Server Receive Data");
            std::array<char, 1024> buffer{};
            asio::error_code err;

            kv.first->read_some(asio::buffer(buffer), err);
            if(err == asio::error::eof) {
                MODULE_ERROR("read client data error {}", err);
                bError = true;
            }
            else {
                kv.second->m_recvData.append(buffer.data());
            }

            if(!bError) {
                if(kv.second->m_sendData.length() > 0) {
//                    auto nLen = asio::write(kv.first, asio::buffer(kv.second->m_sendData.data(), kv.second->m_sendData.length()), err);
                    auto nLen = kv.first->write_some(asio::buffer(kv.second->m_sendData.data(), kv.second->m_sendData.length()));
                    if(nLen > 0) {
                        kv.second->m_sendData.erase(0, nLen);
                    }
                    else if(err == asio::error::eof){
                        MODULE_ERROR("send client data error{}", err);
                        bError = true;
                    }
                }
            }

            if(bError){
                m_removeClient.push_back(kv.first.get());
            }
            else if(kv.second->m_recvData.length() >= 4) {

                const auto *pData = kv.second->m_sendData.data();
                size_t nIndex = 0;
                uint16_t nSize = *(uint16_t*)pData;
                uint16_t nType = *(uint16_t*)(pData + sizeof(uint16_t));

                while((kv.second->m_recvData.length() - nIndex ) >= nSize) {
                    auto itr = m_onReceiveCB.find(nType);
                    if(itr != m_onReceiveCB.end()) {
                        for(const auto& onReceive : itr->second) {
                            (*onReceive)(kv.second->m_guid, nType, kv.second->m_recvData.data() + nIndex, nSize);
                        }
                    }
                    nIndex += nSize;
                }

                if(nIndex != 0){
                    kv.second->m_recvData.erase(0, nIndex);
                }
            }
        }
    }
    return true;
#endif
}

bool NetServer::Shut()
{
    MODULE_INFO("NetService Shut");
    if (m_serverSocket->is_open())
	{
	    m_serverSocket->close();
	    m_serverSocket.reset();
	}

	m_onEnterCB.clear();
	m_onLeaveCB.clear();

	return true;
}

int NetServer::Initialization(const unsigned short nPort, const char *ip /*= nullptr*/)
{
    MODULE_INFO("NetService Initialization");
#ifndef ENABLE_NEW_SOCKET_IMPL
	m_serverSocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

    if(m_serverSocket < 0){
        MODULE_ERROR("unable to create server socket");
    }

	if (ip == nullptr)
	{
		ip = "127.0.0.1";
	}

	sockaddr_in sockAddr{};
	memset(&sockAddr, 0, sizeof(sockAddr));
    sockAddr.sin_family = PF_INET;
    sockAddr.sin_addr.s_addr = inet_addr(ip);
    sockAddr.sin_port = htons(nPort);

	bind(m_serverSocket, (const struct sockaddr *)&sockAddr, sizeof(sockAddr));

	listen(m_serverSocket, 1);

	int mode = 1;
	ioctl(m_serverSocket, FIONBIO, (u_long *)&mode);

	return 0;
#else
    m_serverContext     = std::make_shared<asio::io_context>();
    m_serverSocket      = std::make_shared<tcp::socket>(tcp::socket(*m_serverContext));
    m_serverAcceptor    = std::make_shared<tcp::acceptor>(tcp::acceptor(*m_serverContext, tcp::endpoint(tcp::v4(), nPort)));
    return 0;
#endif
}

bool NetServer::AddReceiveCallBack(const uint32_t nMsgID, const NET_RECEIVE_FUNCTOR_PTR & cb)
{
	m_onReceiveCB[nMsgID].push_back(cb);
	return true;
}

bool NetServer::AddEventCallBack(const NET_EVENT_FUNCTOR_PTR &enter_cb, const NET_EVENT_FUNCTOR_PTR &leave_cb)
{
	m_onEnterCB.push_back(enter_cb);
	m_onLeaveCB.push_back(leave_cb);
	return true;
}

void NetServer::SendMsg(const uint64_t nClientID, void* msg)
{
	auto itr = m_mapUser.find(nClientID);
	if (itr != m_mapUser.end())
	{
		const char *pData = (const char*)msg;
		itr->second->m_sendData.append(pData, *(uint16_t*)(pData));
	}
}

void NetServer::SendMsg(const uint64_t nClientID, int nMsgID, google::protobuf::Message &msg)
{
	auto itr = m_mapUser.find(nClientID);
	if (itr != m_mapUser.end())
	{
		std::string strData;
		msg.SerializeToString(&strData);
		uint16_t nSize = strData.size() + 4;
		uint16_t nType = nMsgID;

		itr->second->m_sendData.append((const char*)&nSize, 2);
		itr->second->m_sendData.append((const char*)&nType, 2);
		itr->second->m_sendData.append(strData.c_str(), strData.size());
	}
}

User* NetServer::GetFreeUser()
{
	User *pUser;
	if (m_listFreeUser.empty())
	{
		pUser = new User();
	}
	else
	{
		pUser = m_listFreeUser.front();
		m_listFreeUser.pop_front();
	}
	pUser->Reset(m_nClientGUID++);
	return pUser;
}

void NetServer::FreeUser(User* pUser)
{
	m_mapUser.erase(pUser->m_guid);
	m_listFreeUser.push_back(pUser);
}

void NetServer::AddClient(tcp::socket *socket)
{
	auto pUser = GetFreeUser();
	m_mapClient.insert(std::make_pair(socket, pUser));
	for (const auto& cb : m_onEnterCB)
	{
		(*cb)(pUser->m_guid);
		m_mapUser.insert(std::make_pair(pUser->m_guid, pUser));
	}
}

void NetServer::RemoveClient(tcp::socket *socket)
{
//     TODO: remove client
//	auto itr = m_mapClient.find();
//	if (itr != m_mapClient.end())
//	{
//		auto pUser = itr->second;
//		for (const auto& cb : m_onLeaveCB)
//		{
//			(*cb)(pUser->m_guid);
//		}
//		FreeUser(pUser);
//		m_mapClient.erase(itr);
//	}
}