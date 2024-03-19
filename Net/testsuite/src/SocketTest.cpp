//
// SocketTest.cpp
//
// Copyright (c) 2005-2006, Applied Informatics Software Engineering GmbH.
// and Contributors.
//
// SPDX-License-Identifier:	BSL-1.0
//


#include "SocketTest.h"
#include "CppUnit/TestCaller.h"
#include "CppUnit/TestSuite.h"
#include "EchoServer.h"
#include "Poco/Net/StreamSocket.h"
#include "Poco/Net/ServerSocket.h"
#include "Poco/Net/SocketAddress.h"
#include "Poco/Net/NetException.h"
#include "Poco/Timespan.h"
#include "Poco/Stopwatch.h"
#include "Poco/Buffer.h"
#include "Poco/FIFOBuffer.h"
#include "Poco/Delegate.h"
#include "Poco/File.h"
#include <iostream>


using Poco::Net::Socket;
using Poco::Net::StreamSocket;
using Poco::Net::ServerSocket;
using Poco::Net::SocketAddress;
using Poco::Net::ConnectionRefusedException;
using Poco::Timespan;
using Poco::Stopwatch;
using Poco::TimeoutException;
using Poco::InvalidArgumentException;
using Poco::Buffer;
using Poco::FIFOBuffer;
using Poco::delegate;


SocketTest::SocketTest(const std::string& name): CppUnit::TestCase(name)
{
}


SocketTest::~SocketTest()
{
}


void SocketTest::testEcho()
{
	EchoServer echoServer;
	StreamSocket ss;
	ss.connect(SocketAddress("127.0.0.1", echoServer.port()));
	int n = ss.sendBytes("hello", 5);
	assertTrue (n == 5);
	char buffer[256];
	n = ss.receiveBytes(buffer, sizeof(buffer));
	assertTrue (n == 5);
	assertTrue (std::string(buffer, n) == "hello");
	ss.close();
}


void SocketTest::testMoveStreamSocket()
{
	EchoServer echoServer;
	StreamSocket ss0 = StreamSocket();
	ss0.connect(SocketAddress("127.0.0.1", echoServer.port()));
	StreamSocket ss(std::move(ss0));
#if POCO_NEW_STATE_ON_MOVE
	assertTrue (ss0.isNull());
#else
	assertFalse (ss0.isNull());
#endif

	char buffer[256];
	std::memset(buffer, 0, sizeof(buffer));
	ss0 = ss;
	assertTrue (ss0.impl());
	assertTrue (ss.impl());
	assertTrue (ss0.impl() == ss.impl());
	ss = std::move(ss0);
#if POCO_NEW_STATE_ON_MOVE
	assertTrue (ss0.isNull());
#else
	assertFalse (ss0.isNull());
#endif
	assertTrue (ss.impl());
	int n = ss.sendBytes("hello", 5);
	assertTrue (n == 5);
	n = ss.receiveBytes(buffer, sizeof(buffer));
	assertTrue (n == 5);
	assertTrue (std::string(buffer, n) == "hello");
	ss.close();
	ss0.close();
}


void SocketTest::testPoll()
{
	EchoServer echoServer;
	StreamSocket ss;
	ss.connect(SocketAddress("127.0.0.1", echoServer.port()));
	Stopwatch sw;
	sw.start();
	Timespan timeout(1000000);
	assertTrue (!ss.poll(timeout, Socket::SELECT_READ));
	assertTrue (sw.elapsed() >= 900000);
	sw.restart();
	assertTrue (ss.poll(timeout, Socket::SELECT_WRITE));
	assertTrue (sw.elapsed() < 100000);
	ss.sendBytes("hello", 5);
	char buffer[256];
	sw.restart();
	assertTrue (ss.poll(timeout, Socket::SELECT_READ));
	assertTrue (sw.elapsed() < 100000);
	int n = ss.receiveBytes(buffer, sizeof(buffer));
	assertTrue (n == 5);
	assertTrue (std::string(buffer, n) == "hello");
	ss.close();
}


void SocketTest::testAvailable()
{
	EchoServer echoServer;
	StreamSocket ss;
	ss.connect(SocketAddress("127.0.0.1", echoServer.port()));
	Timespan timeout(1000000);
	ss.sendBytes("hello", 5);
	char buffer[256];
	assertTrue (ss.poll(timeout, Socket::SELECT_READ));
	int av = ss.available();
	assertTrue (av > 0 && av <= 5);
	int n = ss.receiveBytes(buffer, sizeof(buffer));
	assertTrue (n == 5);
	assertTrue (std::string(buffer, n) == "hello");
	ss.close();
}


void SocketTest::testFIFOBuffer()
{
	Buffer<char> b(5);
	b[0] = 'h';
	b[1] = 'e';
	b[2] = 'l';
	b[3] = 'l';
	b[4] = 'o';

	FIFOBuffer f(5, true);

	f.readable += delegate(this, &SocketTest::onReadable);
	f.writable += delegate(this, &SocketTest::onWritable);

	assertTrue (0 == _notToReadable);
	assertTrue (0 == _readableToNot);
	assertTrue (0 == _notToWritable);
	assertTrue (0 == _writableToNot);
	f.write(b);
	assertTrue (1 == _notToReadable);
	assertTrue (0 == _readableToNot);
	assertTrue (0 == _notToWritable);
	assertTrue (1 == _writableToNot);

	EchoServer echoServer;
	StreamSocket ss;
	ss.connect(SocketAddress("127.0.0.1", echoServer.port()));
	int n = ss.sendBytes(f);
	assertTrue (n == 5);
	assertTrue (1 == _notToReadable);
	assertTrue (1 == _readableToNot);
	assertTrue (1 == _notToWritable);
	assertTrue (1 == _writableToNot);
	assertTrue (f.isEmpty());

	n = ss.receiveBytes(f);
	assertTrue (n == 5);

	assertTrue (2 == _notToReadable);
	assertTrue (1 == _readableToNot);
	assertTrue (1 == _notToWritable);
	assertTrue (2 == _writableToNot);

	assertTrue (f[0] == 'h');
	assertTrue (f[1] == 'e');
	assertTrue (f[2] == 'l');
	assertTrue (f[3] == 'l');
	assertTrue (f[4] == 'o');

	f.readable -= delegate(this, &SocketTest::onReadable);
	f.writable -= delegate(this, &SocketTest::onWritable);

	ss.close();
}


void SocketTest::testConnect()
{
	ServerSocket serv;
	serv.bind(SocketAddress());
	serv.listen();
	StreamSocket ss;
	Timespan timeout(250000);
	ss.connect(SocketAddress("127.0.0.1", serv.address().port()), timeout);
}


void SocketTest::testConnectRefused()
{
	ServerSocket serv;
	serv.bind(SocketAddress());
	serv.listen();
	Poco::UInt16 port = serv.address().port();
	serv.close();
	StreamSocket ss;
	Timespan timeout(250000);
	try
	{
		ss.connect(SocketAddress("127.0.0.1", port));
		fail("connection refused - must throw");
	}
	catch (ConnectionRefusedException&)
	{
	}
}


void SocketTest::testConnectRefusedNB()
{
	ServerSocket serv;
	serv.bind(SocketAddress());
	serv.listen();
	Poco::UInt16 port = serv.address().port();
	serv.close();
	StreamSocket ss;
	Timespan timeout(2, 0);
	try
	{
		ss.connect(SocketAddress("127.0.0.1", port), timeout);
		fail("connection refused - must throw");
	}
	catch (TimeoutException&)
	{
	}
	catch (ConnectionRefusedException&)
	{
	}
}


void SocketTest::testNonBlocking()
{
	EchoServer echoServer;
	StreamSocket ss;
	ss.connect(SocketAddress("127.0.0.1", echoServer.port()));
	ss.setBlocking(false);

	Timespan timeout(1000000);
	assertTrue (ss.poll(timeout, Socket::SELECT_WRITE));
	int n = ss.sendBytes("hello", 5);
	assertTrue (n == 5);

	char buffer[256];
	assertTrue (ss.poll(timeout, Socket::SELECT_READ));
	n = ss.receiveBytes(buffer, sizeof(buffer));
	assertTrue (n == 5);
	assertTrue (std::string(buffer, n) == "hello");
	ss.close();
}



void SocketTest::testAddress()
{
	ServerSocket serv;
	serv.bind(SocketAddress());
	serv.listen();
	StreamSocket ss;
	ss.connect(SocketAddress("127.0.0.1", serv.address().port()));
	StreamSocket css = serv.acceptConnection();
	assertTrue (css.peerAddress().host() == ss.address().host());
	assertTrue (css.peerAddress().port() == ss.address().port());
}


void SocketTest::testAssign()
{
	ServerSocket serv;
	StreamSocket ss1;
	StreamSocket ss2;

	assertTrue (ss1 != ss2);
	StreamSocket ss3(ss1);
	assertTrue (ss1 == ss3);
	ss3 = ss2;
	assertTrue (ss1 != ss3);
	assertTrue (ss2 == ss3);

	try
	{
		ss1 = serv;
		fail("incompatible assignment - must throw");
	}
	catch (InvalidArgumentException&)
	{
	}

	try
	{
		StreamSocket ss4(serv);
		fail("incompatible assignment - must throw");
	}
	catch (InvalidArgumentException&)
	{
	}

	try
	{
		serv = ss1;
		fail("incompatible assignment - must throw");
	}
	catch (InvalidArgumentException&)
	{
	}

	try
	{
		ServerSocket serv2(ss1);
		fail("incompatible assignment - must throw");
	}
	catch (InvalidArgumentException&)
	{
	}
}


void SocketTest::testTimeout()
{
	EchoServer echoServer;
	StreamSocket ss;
	ss.connect(SocketAddress("127.0.0.1", echoServer.port()));

	Timespan timeout0 = ss.getReceiveTimeout();
	Timespan timeout(250000);
	ss.setReceiveTimeout(timeout);
	Timespan timeout1 = ss.getReceiveTimeout();
	std::cout << "original receive timeout:  " << timeout0.totalMicroseconds() << std::endl;
	std::cout << "requested receive timeout: " << timeout.totalMicroseconds() << std::endl;
	std::cout << "actual receive timeout:    " << timeout1.totalMicroseconds() << std::endl;

	// some socket implementations adjust the timeout value
	// assertTrue (ss.getReceiveTimeout() == timeout);
	Stopwatch sw;
	try
	{
		char buffer[256];
		sw.start();
		ss.receiveBytes(buffer, sizeof(buffer));
		fail("nothing to receive - must timeout");
	}
	catch (TimeoutException&)
	{
	}
	assertTrue (sw.elapsed() < 1000000);

	timeout0 = ss.getSendTimeout();
	ss.setSendTimeout(timeout);
	timeout1 = ss.getSendTimeout();
	std::cout << "original send timeout:  " << timeout0.totalMicroseconds() << std::endl;
	std::cout << "requested send timeout: " << timeout.totalMicroseconds() << std::endl;
	std::cout << "actual send timeout:    " << timeout1.totalMicroseconds() << std::endl;
	// assertTrue (ss.getSendTimeout() == timeout);
}


void SocketTest::testBufferSize()
{
	EchoServer echoServer;
	SocketAddress sa("127.0.0.1", 1234);
	StreamSocket ss(sa.family());

	int osz = ss.getSendBufferSize();
	int rsz = 32000;
	ss.setSendBufferSize(rsz);
	int asz = ss.getSendBufferSize();
	std::cout << "original send buffer size:  " << osz << std::endl;
	std::cout << "requested send buffer size: " << rsz << std::endl;
	std::cout << "actual send buffer size:    " << asz << std::endl;

	osz = ss.getReceiveBufferSize();
	ss.setReceiveBufferSize(rsz);
	asz = ss.getReceiveBufferSize();
	std::cout << "original recv buffer size:  " << osz << std::endl;
	std::cout << "requested recv buffer size: " << rsz << std::endl;
	std::cout << "actual recv buffer size:    " << asz << std::endl;
}


void SocketTest::testOptions()
{
	EchoServer echoServer;
	StreamSocket ss;
	ss.connect(SocketAddress("127.0.0.1", echoServer.port()));

	ss.setLinger(true, 20);
	bool f;
	int  t;
	ss.getLinger(f, t);
	assertTrue (f && t == 20);
	ss.setLinger(false, 0);
	ss.getLinger(f, t);
	assertTrue (!f);

	ss.setNoDelay(true);
	assertTrue (ss.getNoDelay());
	ss.setNoDelay(false);
	assertTrue (!ss.getNoDelay());

	ss.setKeepAlive(true);
	assertTrue (ss.getKeepAlive());
	ss.setKeepAlive(false);
	assertTrue (!ss.getKeepAlive());

	ss.setOOBInline(true);
	assertTrue (ss.getOOBInline());
	ss.setOOBInline(false);
	assertTrue (!ss.getOOBInline());
}


void SocketTest::testSelect()
{
	Timespan timeout(250000);

	EchoServer echoServer;
	StreamSocket ss;
	ss.connect(SocketAddress("127.0.0.1", echoServer.port()));

	Socket::SocketList readList;
	Socket::SocketList writeList;
	Socket::SocketList exceptList;

	readList.push_back(ss);
	assertTrue (Socket::select(readList, writeList, exceptList, timeout) == 0);
	assertTrue (readList.empty());
	assertTrue (writeList.empty());
	assertTrue (exceptList.empty());

	ss.sendBytes("hello", 5);

	ss.poll(timeout, Socket::SELECT_READ);

	readList.push_back(ss);
	writeList.push_back(ss);
	assertTrue (Socket::select(readList, writeList, exceptList, timeout) == 2);
	assertTrue (!readList.empty());
	assertTrue (!writeList.empty());
	assertTrue (exceptList.empty());

	char buffer[256];
	int n = ss.receiveBytes(buffer, sizeof(buffer));
	assertTrue (n == 5);
	assertTrue (std::string(buffer, n) == "hello");
	ss.close();
}


void SocketTest::testSelect2()
{
	Timespan timeout(100000);

	EchoServer echoServer1;
	EchoServer echoServer2;
	StreamSocket ss1(SocketAddress("127.0.0.1", echoServer1.port()));
	StreamSocket ss2(SocketAddress("127.0.0.1", echoServer2.port()));

	Socket::SocketList readList;
	Socket::SocketList writeList;
	Socket::SocketList exceptList;

	readList.push_back(ss1);
	readList.push_back(ss2);
	assertTrue (Socket::select(readList, writeList, exceptList, timeout) == 0);
	assertTrue (readList.empty());
	assertTrue (writeList.empty());
	assertTrue (exceptList.empty());

	ss1.sendBytes("hello", 5);

	ss1.poll(timeout, Socket::SELECT_READ);

	readList.push_back(ss1);
	readList.push_back(ss2);
	assertTrue (Socket::select(readList, writeList, exceptList, timeout) == 1);

	assertTrue (readList.size() == 1);
	assertTrue (readList[0] == ss1);
	assertTrue (writeList.empty());
	assertTrue (exceptList.empty());

	char buffer[256];
	int n = ss1.receiveBytes(buffer, sizeof(buffer));
	assertTrue (n == 5);

	readList.clear();
	writeList.clear();
	exceptList.clear();
	writeList.push_back(ss1);
	writeList.push_back(ss2);
	assertTrue (Socket::select(readList, writeList, exceptList, timeout) == 2);
	assertTrue (readList.empty());
	assertTrue (writeList.size() == 2);
	assertTrue (writeList[0] == ss1 || writeList[1] == ss1);
	assertTrue (writeList[0] == ss2 || writeList[1] == ss2);
	assertTrue (exceptList.empty());

	ss1.close();
	ss2.close();
}


void SocketTest::testSelect3()
{
	Socket::SocketList readList;
	Socket::SocketList writeList;
	Socket::SocketList exceptList;
	Timespan timeout(1000);

	int rc = Socket::select(readList, writeList, exceptList, timeout);
	assertTrue (rc == 0);
}


void SocketTest::testEchoUnixLocal()
{
#if defined(POCO_OS_FAMILY_UNIX)
#if POCO_OS == POCO_OS_ANDROID
	Poco::File socketFile("/data/local/tmp/SocketTest.sock");
#else
	Poco::File socketFile("/tmp/SocketTest.sock");
#endif
	if (socketFile.exists()) socketFile.remove();
	SocketAddress localAddr(SocketAddress::UNIX_LOCAL, socketFile.path());
	EchoServer echoServer(localAddr);
	StreamSocket ss(SocketAddress::UNIX_LOCAL);
	ss.connect(localAddr);
	int n = ss.sendBytes("hello", 5);
	assertTrue (n == 5);
	char buffer[256];
	n = ss.receiveBytes(buffer, sizeof(buffer));
	assertTrue (n == 5);
	assertTrue (std::string(buffer, n) == "hello");
	ss.close();
	socketFile.remove();
#endif
}



void SocketTest::onReadable(bool& b)
{
	if (b) ++_notToReadable;
	else ++_readableToNot;
};


void SocketTest::onWritable(bool& b)
{
	if (b) ++_notToWritable;
	else ++_writableToNot;
}


void SocketTest::setUp()
{
	_readableToNot = 0;
	_notToReadable = 0;
	_writableToNot = 0;
	_notToWritable = 0;
}


void SocketTest::tearDown()
{
}


CppUnit::Test* SocketTest::suite()
{
	CppUnit::TestSuite* pSuite = new CppUnit::TestSuite("SocketTest");

	CppUnit_addTest(pSuite, SocketTest, testEcho);
	CppUnit_addTest(pSuite, SocketTest, testMoveStreamSocket);
	CppUnit_addTest(pSuite, SocketTest, testPoll);
	CppUnit_addTest(pSuite, SocketTest, testAvailable);
	CppUnit_addTest(pSuite, SocketTest, testFIFOBuffer);
	CppUnit_addTest(pSuite, SocketTest, testConnect);
	CppUnit_addTest(pSuite, SocketTest, testConnectRefused);
	CppUnit_addTest(pSuite, SocketTest, testConnectRefusedNB);
	CppUnit_addTest(pSuite, SocketTest, testNonBlocking);
	CppUnit_addTest(pSuite, SocketTest, testAddress);
	CppUnit_addTest(pSuite, SocketTest, testAssign);
	CppUnit_addTest(pSuite, SocketTest, testTimeout);
	CppUnit_addTest(pSuite, SocketTest, testBufferSize);
	CppUnit_addTest(pSuite, SocketTest, testOptions);
	CppUnit_addTest(pSuite, SocketTest, testSelect);
	CppUnit_addTest(pSuite, SocketTest, testSelect2);
	CppUnit_addTest(pSuite, SocketTest, testSelect3);
	CppUnit_addTest(pSuite, SocketTest, testEchoUnixLocal);

	return pSuite;
}
