# Redis-CPP: A Simple Redis-like Server & Client in C++

## Overview

This project is a lightweight, educational implementation of a Redis-like server and a corresponding command-line client, written entirely in C++. It demonstrates core networking concepts such as non-blocking I/O, connection polling, and protocol parsing in a single-threaded environment.

The server and client communicate using a subset of the Redis Serialization Protocol (RESP).

## Features

*   **Non-Blocking TCP Server:** Built using `poll()` to handle multiple client connections concurrently without threads.
*   **RESP Implementation:** Parses and responds to commands formatted according to the RESP specification.
*   **Supported Commands:**
    *   `PING`: The server replies with `PONG`.
    *   `ECHO <message>`: The server replies with `<message>`.
*   **Interactive Client:** A command-line interface to interact with the server.
*   **Client-side Pipelining:** The client supports batching commands using `BEGIN` and `EXEC` for improved efficiency.

## Code Structure

*   `server.cpp`: Implements the server logic, including accepting connections, reading requests, parsing RESP commands, and writing responses.
*   `client.cpp`: Implements the interactive client, which can send commands to the server and display the responses.
*   `.gitignore`: Standard gitignore file for C++ projects, configured to ignore compiled executables.
*   `README.md`: You are here!

## Getting Started

### Prerequisites

You will need a C++ compiler that supports C++11 or later (e.g., `g++` or `clang++`).

### Building

To compile the server and client, you can use the following commands. The executables `server` and `client` will be created.

```bash
# Compile the server
g++ -std=c++11 -o server server.cpp

# Compile the client
g++ -std=c++11 -o client client.cpp
```

### Usage

1.  **Start the Server**

    Open a terminal and run the compiled server executable. It will listen for connections on port 3000.

    ```bash
    ./server
    ```
    You should see the message: `Server listening on port 3000...`

2.  **Run the Client**

    Open a second terminal and connect to the server using the client executable.

    ```bash
    ./client
    ```
    You will be greeted with a `>` prompt.

3.  **Send Commands**

    You can now send commands to the server. Type `help` in the client for a list of commands.

    **Simple Commands:**
    ```
    > ping
    PONG
    > echo hello world
    hello world
    ```

    **Pipelining:**
    ```
    > begin
    Pipeline mode started. Type 'exec' to send.
    pipe> ping
    QUEUED
    pipe> echo first
    QUEUED
    pipe> echo second
    QUEUED
    pipe> exec
    --- Pipeline Responses ---
    PONG
    first
    second
    ------------------------
    >
    ```

    **Exiting the client:**
    ```
    > quit
    ```
