

/* Code Editor */

// Variables
string multiTextBoxText = "# Write Your Code";
bool multiTextBoxEditMode = false;


static void codeEditor(void)
{
    bool run_button = Button(screenWidth*.5, screenHeight*.1, 80, 30, "Run");

//    bool run_button = Button(screenWidth*.2, screenHeight*.2, 80, 30, "Run");


    if (run_button)
    {
        cout << "Starting Compilation of Game\n";

        // Initialize the Python interpreter
        Py_Initialize();

        // Pass the values of the C++ variables to the Python interpreter
        PyObject* py_code_to_run = PyUnicode_FromString(multiTextBoxText.data());
        PyObject* main_module = PyImport_AddModule("__main__");
        PyObject* global_dict = PyModule_GetDict(main_module);
        PyDict_SetItemString(global_dict, "code_to_run", py_code_to_run);

        // Define the Python code that you want to execute
        const char* python_code =
            "print('Compiler Called')\n"
            "#print(code_to_run)\n"
            "exec(code_to_run)\n";

        // Execute the Python code
        PyRun_SimpleString(python_code);

        // Clean up and exit
        Py_Finalize();

        cout << "\nCompilation Ended\n\n";
        
    }
    else
    {
        cout << "";
    }

    multiTextBoxText.resize(999999999);

    if (GuiTextBoxMulti((Rectangle){ screenWidth*.2, screenHeight*.57, screenWidth*.4, screenHeight*.3 }, multiTextBoxText.data(), multiTextBoxText.length(), multiTextBoxEditMode)) multiTextBoxEditMode = !multiTextBoxEditMode;
    
}


