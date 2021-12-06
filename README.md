# epmon
This project is an implementation of the task described in the EndpointAgentInterviewProject11.2021.pdf file,
implemented for Ubuntu Linux. It may work on other Linux flavors as well but has not been tested elsewhere.
I used the CLion IDE from JetBrains which uses CMake for the build process.

The project makes use of the following libraries.

- For logging I use [spdlog](https://github.com/gabime/spdlog) by installing libspdlog-dev.
- For JSON handling I use Niels Lohmann's [json](https://github.com/nlohmann) by copying the single header file to my project directory. It's
included in the source code, so no need to install anything there.
- For dealing with the process information I use the procps library by installing libprocps-dev.
- I use libcurl for HTTP operations but that was already installed.
- I used libpthread for thread support but that was also already installed.

To successfully compile and run the project you may need to install one or more of the above libraries.

The compiled program is a command line app with minimal console output. It logs messages to a file named epmon_log.txt
in the logs subdirectory of the directory in which you run the program. It runs until interrupted by Ctrl-C.
There are four configuration parameters that can be specified on the command line. Note that if you want to specify
any parameter, you must specify all four. There are reasonable default values provided but you will most likely need
to specify your own configuration server URL and results server URL.
