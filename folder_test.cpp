#include <iostream>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <vector>
#include <string>
#include <algorithm>

using namespace std;

int main(int argc, char *argv[])
{
    DIR *dir;
    struct dirent *ent;
    struct stat st;
    vector<string> files;
    vector<string> folders;
    if (argc != 2) {
        cout << "Usage: ./list <directory>" << endl;
        return 1;
    }

    if ((dir = opendir(dir_path)) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            string file = ent->d_name;
            if (file == "." || file == "..") {
                continue;
            }
            string path = dir_path;
            path += "/";
            path += file;
            if (stat(path.c_str(), &st) == -1) {
                cout << "Error: " << strerror(errno) << endl;
                return 1;
            }
            if (S_ISDIR(st.st_mode)) {
                folders.push_back(file);
            } else {
                files.push_back(file);
            }
        }
        sort(files.begin(), files.end());
        sort(folders.begin(), folders.end());
        for (int i = 0; i < folders.size(); i++) {
            cout << folders[i] << " (folder)" << endl;
        }
        for (int i = 0; i < files.size(); i++) {
            cout << files[i] << " (file)" << endl;
        }
        closedir(dir);
    } else {
        cout << "Error: " << strerror(errno) << endl;
        return 1;
    }
    return 0;
}
