/*

 The MIT License (MIT)

 Copyright (c) 2015 Jonas Schnelli

 Permission is hereby granted, free of charge, to any person obtaining
 a copy of this software and associated documentation files (the "Software"),
 to deal in the Software without restriction, including without limitation
 the rights to use, copy, modify, merge, publish, distribute, sublicense,
 and/or sell copies of the Software, and to permit persons to whom the
 Software is furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included
 in all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES
 OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 OTHER DEALINGS IN THE SOFTWARE.

*/

#include <assert.h>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include <string>

#include "../include/dbb.h"
#include "util.h"

#include "../include/univalue.h"
#include "hidapi/hidapi.h"
#include "openssl/sha.h"

//simple dispatch class for a dbb command
class CDBBCommand
{
public:
    std::string cmdname;
    std::string json;
    bool requiresEncryption;
};

static const CDBBCommand vCommands[] =
{
    { "erase"           , "{\"reset\" : \"__ERASE__\"}",                                false},
    { "password"        , "{\"password\" : \"%!newpassword%\"",                         false},
    { "led"             , "{\"led\" : \"toggle\"}",                                     true},
    { "seed"            , "{\"seed\" : {\"source\" :\"%source|create%\","
                            "\"decrypt\": \"%decrypt|no%\","
                            "\"salt\" : \"%salt%\"} }",                                 true},

    { "backuplist"      , "{\"backup\" : \"list\"}",                                    true},
    { "backuperase"     , "{\"backup\" : \"erase\"}",                                   true},
    { "backup"          , "{\"backup\" : { \"encrypt\":\"%encrypt|no%\","
                            "\"filename\": \"%filename|backup.dat%\"}}",                true},

    { "sign"            , "{\"sign\" : { \"type\":\"%type|transaction%\","
                            "\"data\": \"%!data%\","
                            "\"keypath\": \"%!keypath%\","
                            "\"change_keypath\": \"%!changekeypath%\"}}",               true},

    { "xpub"            , "{\"xpub\" : \"%!keypath%\"}",                                true},

    { "name"            , "{\"name\" : \"%!name%\"}",                                   true},
    { "random"          , "{\"random\" : \"%mode|true%\"}",                             true},
    { "sn"              , "{\"device\" : \"serial\"}",                                  true},
    { "version"         , "{\"device\" : \"version\"}",                                 true},

    { "lock"            , "{\"device\" : \"lock\"}",                                    true},
    { "verifypass"      , "{\"verifypass\" : \"%operation|create%\"}",                  true},

    { "aes"             , "{\"aes256cbc\" : { \"type\":\"%type|encrypt%\","
                            "\"data\": \"%!data%\"}}",                                  true},

    //TODO add all missing commands
    //parameters could be added with something like "{\"seed\" : {\"source\" : \"$1\"} }" (where $1 will be replace with argv[1])
};

int main( int argc, char *argv[] )
{
    ParseParameters(argc, argv);
    
    if (!DBB::openConnection())
        printf("Error: No digital bitbox connected\n");
    
    else {
        DebugOut("main", "Digital Bitbox Connected\n");

        if (argc < 2)
        {
            printf("no command given\n");
            return 0;
        }
        unsigned int i = 0, loop = 0;
        bool cmdfound = false;
        std::string userCmd;

        //search after first argument with no -
        std::vector<std::string> cmdArgs;
        for (int i = 1; i < argc; i++)
            if (strlen(argv[i]) > 0 && argv[i][0] != '-')
                cmdArgs.push_back(std::string(argv[i]));

        if (cmdArgs.size() > 0)
            userCmd = cmdArgs.front();

        bool help = false;
        if (userCmd == "help")
        {
            help = true;
            printf("Help:\n");
        }

        //try to find the command in the dispatch table
        for (i = 0; i < (sizeof(vCommands) / sizeof(vCommands[0])); i++)
        {
            CDBBCommand cmd = vCommands[i];
            if (help)
            {
                printf("%s\n", cmd.cmdname.c_str());
            }

            if (cmd.cmdname == userCmd)
            {
                std::string cmdOut;
                std::string json = cmd.json;
                size_t index = 0;

                //replace %vars% in json string with cmd args

                int tokenOpenPos = -1;
                int defaultDelimiterPos = -1;
                std::string var;
                std::string defaultValue;

                for (unsigned int sI=0;sI<json.length();sI++)
                {
                    if (json[sI] == '|' && tokenOpenPos > 0)
                        defaultDelimiterPos = sI;

                    if (json[sI] == '%' && tokenOpenPos < 0)
                        tokenOpenPos = sI;

                    else if (json[sI] == '%' && tokenOpenPos >= 0)
                    {
                        if (defaultDelimiterPos >= 0)
                        {
                            var = json.substr(tokenOpenPos+1, defaultDelimiterPos-tokenOpenPos-1);
                            defaultValue = json.substr(defaultDelimiterPos+1, sI-defaultDelimiterPos-1);
                        }
                        else
                        {
                            var = json.substr(tokenOpenPos+1, sI-tokenOpenPos-1);
                        }

                        //always erase
                        json.erase(tokenOpenPos,sI-tokenOpenPos+1);

                        bool mandatory = false;
                        if (var.size() > 0 && var[0] == '!')
                        {
                            var.erase(0,1);
                            mandatory = true;
                        }

                        var = "-"+var; //cmd args come in over "-arg"
                        if (mapArgs.count(var))
                        {
                            json.insert(tokenOpenPos, mapArgs[var]);
                        }
                        else if(mandatory)
                        {
                            printf("Argument %s is mandatory for command %s\n", var.c_str(), cmd.cmdname.c_str());
                            return 0;
                        }
                        else
                            if (defaultDelimiterPos > 0 && !defaultValue.empty())
                                json.insert(tokenOpenPos, defaultValue);

                        tokenOpenPos = -1;
                        defaultDelimiterPos = -1;
                    }
                }

                if (cmd.requiresEncryption || mapArgs.count("-password"))
                {
                    if (!mapArgs.count("-password"))
                    {
                        printf("This command requires the -password argument\n");
                        return 0;
                    }

                    if (!cmd.requiresEncryption)
                    {
                        DebugOut("main", "Using encyption because -password was set\n");
                    }

                    std::string password = GetArg("-password", "0000"); //0000 will never be used because setting a password is required
                    std::string base64str;
                    std::string unencryptedJson;

                    DebugOut("main", "encrypting raw json: %s\n", json.c_str());
                    DBB::encryptAndEncodeCommand(json, password, base64str);
                    DBB::sendCommand(base64str, cmdOut);
                    try {
                        //hack: decryption needs the new password in case the AES256CBC password has changed
                        if (mapArgs.count("-newpassword"))
                            password = GetArg("-newpassword", "");

                        DBB::decryptAndDecodeCommand(cmdOut, password, unencryptedJson);
                    } catch (const std::exception& ex) {
                        printf("%s\n", ex.what());
                        exit(0);
                    }

                    //example json en/decode
                    UniValue json;
                    json.read(unencryptedJson);
                    std::string jsonFlat = json.write(2); //pretty print with a intend of 2
                    printf("result: %s\n", jsonFlat.c_str());
                }
                else
                {
                    //send command unencrypted
        	        DBB::sendCommand(json, cmdOut);
                    printf("result: %s\n", cmdOut.c_str());
                }
                cmdfound = true;
            }
        }

        if (!cmdfound)
        {
            //try to send it as raw json
            if (userCmd.size() > 1 && userCmd.at(0) == '{') //todo: ignore whitespace
            {
                std::string cmdOut;
                DebugOut("main", "Send raw json %s\n", userCmd.c_str());
        	    cmdfound = DBB::sendCommand(userCmd, cmdOut);
            }

            printf("command (%s) not found\n", SanitizeString(userCmd).c_str());
        }
    }
    return 0;
}
