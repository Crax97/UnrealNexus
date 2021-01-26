
#include "UnrealNexusFile.h"
#include "Misc/CString.h"
#include "Misc/FileHelper.h"
#include "CoreTypes.h"
#include "Misc/AutomationTest.h"
#include <NexusPlugin/nexus/common/dag.h>

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealNexusPluginTest, "Nexus.Tests", EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter )

bool FUnrealNexusPluginTest::RunTest(const FString& Parameters)
{
    
    auto TestFilePath = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("testfile"));
    FFileHelper::SaveStringToFile(TEXT(""), *TestFilePath);
    // Testing file writing
    char TestText[] = "Hello\nWorld\n";
    {
        FUnrealNexusFile* File = new FUnrealNexusFile();
        File->setFileName(TCHAR_TO_ANSI(*TestFilePath));
        TestTrue(TEXT("Opening a file created must work"), File->open(nx::NexusFile::Write));
        TestEqual(TEXT("Writing HelloWorld writes 12 characters"), File->write(TestText, 12), 12);
        delete File;
    }

    // Testing file reading
    {
        FUnrealNexusFile* File = new FUnrealNexusFile();
        File->setFileName(TCHAR_TO_ANSI(*TestFilePath));
        TestTrue(TEXT("Opening a file created must work"), File->open(nx::NexusFile::Read));
        TestEqual(TEXT("File size must be 12"), File->size(), 12);
        char* buf = new char[File->size() + 1];
        buf[File->size()] = '\0';
        TestEqual(TEXT("Reading from file should return 12 characters"), File->read(buf, File->size()), 12);
        TestTrue(TEXT("Test text and read content must be equal"), TCString<char>::Strcmp(buf, TestText) == 0);
        delete buf;
        
        char* map = static_cast<char*>(File->map(0, 5));
        char* mapcpy = new char[6];
        TCString<char>::Strcpy(mapcpy, 5, map);
        mapcpy[5] = '\0';
        TestTrue(TEXT("The first 5 characters of the file must be Hello"),TCString<char>::Strcmp(mapcpy, "Hello") == 0);
        TestTrue(TEXT("The file must be unmap correctly"), File->unmap(static_cast<void*>(map)));
        delete File;
        delete mapcpy;
    }

    // Testing Nexus header reading
    {
        FUnrealNexusFile* File = new FUnrealNexusFile();
        File->setFileName(TCHAR_TO_ANSI(*FPaths::Combine(FPaths::ProjectContentDir(), TEXT("skull.nxs"))));
        File->open(NexusFile::Read);
        
        char* HeaderBuf = new char[88];
        TestEqual(TEXT("Reading header from skull.nxs failed"), File->read(HeaderBuf, 88), sizeof(nx::Header));
        uint32 Magic = *(HeaderBuf);
        TestEqual(TEXT("Magic from skull.nxs not valid"), Magic, 0x4E787320);

        delete File;
    }
    return true;
}


#endif