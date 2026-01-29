# NetworkModuleTest

A high-performance asynchronous network module project for distributed server architecture.

## 📋 Project Overview

This project implements a cross-platform asynchronous network engine with support for multiple I/O mechanisms optimized for different operating systems.

### 🏗️ Architecture

```
Client ←→ TestServer ←→ DBServer
   ↑           ↑            ↑
   │           │            │
Client/    ServerEngine    ServerEngine
Network   (Network)      (Database)
```

### 📁 Project Structure

```
NetworkModuleTest/
├── 📚 Doc/                           # Project documentation
│   ├── ProjectOverview.md              # Main project overview
│   ├── Architecture.md               # Architecture specification
│   ├── API.md                       # API documentation
│   ├── Protocol.md                  # Protocol specification
│   ├── Development.md               # Development guide
│   └── DevelopmentGuide.md          # Detailed development guide
├── 🖥️ Server/                        # Server applications
│   ├── ServerEngine/                 # Network/DB/Stream utility engine
│   │   ├── Core/                    # Core network abstraction layer
│   │   ├── Platforms/               # Platform-specific implementations
│   │   │   ├── Windows/            # Windows IOCP/RIO
│   │   │   ├── Linux/              # Linux epoll/io_uring
│   │   │   └── macOS/              # macOS kqueue
│   │   ├── Protocols/               # Communication protocols
│   │   ├── Tests/                  # Unit tests
│   │   └── Utils/                  # Utilities
│   ├── TestServer/                  # Logic processing server
│   └── DBServer/                    # Database processing server
├── 📡 Client/                        # Client communication module
│   └── Network/                     # Network communication
├── 📋 ModuleTest/                   # Module tests and examples
│   ├── DBModuleTest/                # Database module tests
│   └── MultiPlatformNetwork/        # Cross-platform network tests
│       └── Doc/                    # Detailed network documentation
└── 🔧 Tools/                        # Build and test tools
```

## 🎯 Module Overview

### 1. ServerEngine (Network Engine)
- **Purpose**: Integrated utilities for network, database, stream, time, logging
- **Components**:
  - **Core**: Core network abstraction layer
  - **Platforms**: Platform-specific async I/O implementations
  - **Protocols**: Protobuf-based communication protocols
  - **Utils**: Time, buffer, thread, logging utilities
- **Status**: 🔄 In Progress

### 2. MultiPlatformNetwork (Cross-Platform Network)
- **Purpose**: Cross-platform asynchronous network support
- **Platforms**: Windows (IOCP/RIO), Linux (epoll/io_uring), macOS (kqueue)
- **Status**: ✅ Completed (archived reference)

### 3. TestServer (Logic Server)
- **Purpose**: Client request processing and business logic
- **Features**:
  - Client connection management
  - Request authentication and authorization
  - Business logic processing
  - DBServer communication
- **Status**: ⏳ Pending

### 4. DBServer (Database Server)
- **Purpose**: Dedicated database CRUD operations
- **Features**:
  - Data query/insert/update/delete
  - Transaction management
  - Connection pool management
- **Status**: ⏳ Pending

### 5. Client/Network (Client Communication)
- **Purpose**: Specialized client module for communication
- **Features**:
  - Server connection management
  - Message send/receive
  - Auto-reconnection
- **Status**: ⏳ Pending

## 🚀 Technology Stack

- **Language**: C++17
- **Build**: CMake + Visual Studio
- **Network**: AsyncIO (IOCP/epoll/kqueue)
- **Serialization**: Protocol Buffers (Protobuf)
- **Database**: TBD (MySQL/PostgreSQL/SQLite)
- **Testing**: Google Test
- **Documentation**: Markdown

## 📊 Development Status

| Module | Status | Progress | Notes |
|--------|--------|----------|-------|
| ServerEngine | 🔄 In Progress | 60% | Core, Utilities |
| MultiPlatformNetwork | ✅ Completed | 100% | Reference archive |
| TestServer | ⏳ Pending | 0% | Depends on ServerEngine |
| DBServer | ⏳ Pending | 0% | Depends on ServerEngine |
| Client/Network | ⏳ Pending | 0% | Depends on ServerEngine |
| Documentation | ✅ Updated | 95% | Comprehensive docs |

## 🔧 Platform Support

### Windows
- **Primary**: IOCP (I/O Completion Ports)
- **Advanced**: RIO (Registered I/O) - Windows 8+
- **VS Project**: ServerEngine.vcxproj

### Linux
- **Primary**: epoll
- **Advanced**: io_uring - Linux 5.1+
- **Build**: CMake

### macOS
- **Primary**: kqueue
- **Advanced**: kqueue optimizations
- **Build**: CMake

## 📖 Documentation

### Core Documentation
- [ProjectOverview.md](./Doc/ProjectOverview.md) - Main project overview (Korean)
- [Architecture.md](./Doc/Architecture.md) - Architecture specification
- [API.md](./Doc/API.md) - API documentation
- [Protocol.md](./Doc/Protocol.md) - Protocol specification
- [Development.md](./Doc/Development.md) - Development guide
- [DevelopmentGuide.md](./Doc/DevelopmentGuide.md) - Detailed development guide

### MultiPlatform Network Documentation
- [MultiPlatformNetwork/Doc/README.md](./ModuleTest/MultiPlatformNetwork/Doc/README.md) - Comprehensive network documentation
- [01_IOCP_Architecture_Analysis.md](./ModuleTest/MultiPlatformNetwork/Doc/01_IOCP_Architecture_Analysis.md) - IOCP analysis
- [02_Coding_Conventions_Guide.md](./ModuleTest/MultiPlatformNetwork/Doc/02_Coding_Conventions_Guide.md) - Coding standards
- [06_Cross_Platform_Architecture.md](./ModuleTest/MultiPlatformNetwork/Doc/06_Cross_Platform_Architecture.md) - Cross-platform design

## 🏃‍♂️ Quick Start

### Prerequisites
- Visual Studio 2019+ (Windows) or GCC 7+ (Linux/macOS)
- CMake 3.15+
- Protocol Buffers compiler
- Google Test (for testing)

### Build Instructions

#### Windows (Visual Studio)
```bash
# Open NetworkModuleTest.sln in Visual Studio
# Build solution or specific projects
```

#### Linux/macOS (CMake)
```bash
mkdir build
cd build
cmake ..
make -j4
```

### Running Tests
```bash
# Build and run unit tests
./build/tests/AsyncIOTest
```

## 🔄 Next Steps

1. **Complete ServerEngine**
   - Core network engine implementation
   - Utilities library implementation
   - Protobuf integration

2. **Implement TestServer**
   - ServerEngine integration
   - Client processing logic
   - DBServer communication preparation

3. **Implement DBServer**
   - ServerEngine integration
   - Database connectivity
   - Transaction processing

4. **Implement Client/Network**
   - Simple communication interface
   - Auto-reconnection functionality
   - Message queue management

## 🤝 Contributing

1. Read the [Coding Conventions Guide](./ModuleTest/MultiPlatformNetwork/Doc/02_Coding_Conventions_Guide.md)
2. Follow the [Development Guide](./Doc/DevelopmentGuide.md)
3. Ensure all code follows the established patterns
4. Add comprehensive tests for new features
5. Update documentation

## 📄 License

This project is licensed under the MIT License - see the LICENSE file for details.

## 📞 Contact

- **Issues**: Please use GitHub Issues
- **Documentation**: See [Doc/](./Doc/) folder
- **Development**: See [DevelopmentGuide.md](./Doc/DevelopmentGuide.md)

---

*This project is actively being developed. Documentation is updated as progress is made.*