Build guide
quectel@quectel-OptiPlex-7090:~/Wakaama/05_Wakaama/03_client$ make clean
quectel@quectel-OptiPlex-7090:~/Wakaama/05_Wakaama/03_client$ rm CMakeCache.txt
quectel@quectel-OptiPlex-7090:~/Wakaama/05_Wakaama/03_client$ cmake ./examples/client
quectel@quectel-OptiPlex-7090:~/Wakaama/05_Wakaama/03_client$ make

quectel@quectel-OptiPlex-7090:~/Wakaama/05_Wakaama/02_bs_server$ make clean
quectel@quectel-OptiPlex-7090:~/Wakaama/05_Wakaama/02_bs_server$ rm CMakeCache.txt
quectel@quectel-OptiPlex-7090:~/Wakaama/05_Wakaama/02_bs_server$ cmake ./examples/bootstrap_server
quectel@quectel-OptiPlex-7090:~/Wakaama/05_Wakaama/02_bs_server$ make

quectel@quectel-OptiPlex-7090:~/Wakaama/05_Wakaama/05_server$ rm CMakeCache.txt
quectel@quectel-OptiPlex-7090:~/Wakaama/05_Wakaama/05_server$ make clean
quectel@quectel-OptiPlex-7090:~/Wakaama/05_Wakaama/05_server$ cmake ./examples/server
quectel@quectel-OptiPlex-7090:~/Wakaama/05_Wakaama/05_server$ make

quectel@quectel-OptiPlex-7090:~/Wakaama/05_Wakaama/07_sender$ rm -rf build
quectel@quectel-OptiPlex-7090:~/Wakaama/05_Wakaama/07_sender$ cmake -S . -B build
quectel@quectel-OptiPlex-7090:~/Wakaama/05_Wakaama/07_sender$ cmake --build build

