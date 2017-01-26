/*
This Source Code Form is subject to the
terms of the Mozilla Public License, v.
2.0. If a copy of the MPL was not
distributed with this file, You can
obtain one at
http://mozilla.org/MPL/2.0/.
*/
/*
    MaSzyna EU07 locomotive simulator
    Copyright (C) 2001-2004  Marcin Wozniak, Maciej Czapkiewicz and others

*/

#include "stdafx.h"
#include "World.h"

#include "opengl/glew.h"
#include "opengl/glut.h"

#include "Globals.h"
#include "Logs.h"
#include "MdlMngr.h"
#include "Texture.h"
#include "Timer.h"
#include "mtable.h"
#include "Sound.h"
#include "Camera.h"
#include "ResourceManager.h"
#include "Event.h"
#include "Train.h"
#include "Driver.h"
#include "Console.h"

#define TEXTURE_FILTER_CONTROL_EXT 0x8500
#define TEXTURE_LOD_BIAS_EXT 0x8501
//---------------------------------------------------------------------------

typedef void(APIENTRY *FglutBitmapCharacter)(void *font, int character); // typ funkcji
FglutBitmapCharacter glutBitmapCharacterDLL = NULL; // deklaracja zmiennej
HINSTANCE hinstGLUT32 = NULL; // wskaźnik do GLUT32.DLL
// GLUTAPI void APIENTRY glutBitmapCharacterDLL(void *font, int character);
TDynamicObject *Controlled = NULL; // pojazd, który prowadzimy

const double fTimeMax = 1.00; //[s] maksymalny czas aktualizacji w jednek klatce

TWorld::TWorld()
{
    // randomize();
    // Randomize();
    Train = NULL;
    // Aspect=1;
    for (int i = 0; i < 10; ++i)
        KeyEvents[i] = NULL; // eventy wyzwalane klawiszami cyfrowymi
    Global::iSlowMotion = 0;
    // Global::changeDynObj=NULL;
    OutText1 = ""; // teksty wyświetlane na ekranie
    OutText2 = "";
    OutText3 = "";
    iCheckFPS = 0; // kiedy znów sprawdzić FPS, żeby wyłączać optymalizacji od razu do zera
    pDynamicNearest = NULL;
    fTimeBuffer = 0.0; // bufor czasu aktualizacji dla stałego kroku fizyki
    fMaxDt = 0.01; //[s] początkowy krok czasowy fizyki
    fTime50Hz = 0.0; // bufor czasu dla komunikacji z PoKeys
}

TWorld::~TWorld()
{
    Global::bManageNodes = false; // Ra: wyłączenie wyrejestrowania, bo się sypie
    TrainDelete();
    // Ground.Free(); //Ra: usunięcie obiektów przed usunięciem dźwięków - sypie się
    TSoundsManager::Free();
    TModelsManager::Free();
    TTexturesManager::Free();
    glDeleteLists(base, 96);
    if (hinstGLUT32)
        FreeLibrary(hinstGLUT32);
}

void TWorld::TrainDelete(TDynamicObject *d)
{ // usunięcie pojazdu prowadzonego przez użytkownika
    if (d)
        if (Train)
            if (Train->Dynamic() != d)
                return; // nie tego usuwać
    delete Train; // i nie ma czym sterować
    Train = NULL;
    Controlled = NULL; // tego też już nie ma
    mvControlled = NULL;
    Global::pUserDynamic = NULL; // tego też nie ma
};

GLvoid TWorld::glPrint(const char *txt) // custom GL "Print" routine
{ // wypisywanie tekstu 2D na ekranie
    if (!txt)
        return;
    if (Global::bGlutFont)
    { // tekst generowany przez GLUT
        int i, len = strlen(txt);
        for (i = 0; i < len; i++)
            glutBitmapCharacterDLL(GLUT_BITMAP_8_BY_13, txt[i]); // funkcja linkowana dynamicznie
    }
    else
    { // generowanie przez Display Lists
        glPushAttrib(GL_LIST_BIT); // pushes the display list bits
        glListBase(base - 32); // sets the base character to 32
        glCallLists(strlen(txt), GL_UNSIGNED_BYTE, txt); // draws the display list text
        glPopAttrib(); // pops the display list bits
    }
}

/* Ra: do opracowania: wybor karty graficznej ~Intel gdy są dwie...
BOOL GetDisplayMonitorInfo(int nDeviceIndex, LPSTR lpszMonitorInfo)
{
    FARPROC EnumDisplayDevices;
    HINSTANCE  hInstUser32;
    DISPLAY_DEVICE DispDev;
    char szSaveDeviceName[33];  // 32 + 1 for the null-terminator
    BOOL bRet = TRUE;
        HRESULT hr;

    hInstUser32 = LoadLibrary("c:\\windows\User32.DLL");
    if (!hInstUser32) return FALSE;

    // Get the address of the EnumDisplayDevices function
    EnumDisplayDevices = (FARPROC)GetProcAddress(hInstUser32,"EnumDisplayDevicesA");
    if (!EnumDisplayDevices) {
        FreeLibrary(hInstUser32);
        return FALSE;
    }

    ZeroMemory(&DispDev, sizeof(DispDev));
    DispDev.cb = sizeof(DispDev);

    // After the first call to EnumDisplayDevices,
    // DispDev.DeviceString is the adapter name
    if (EnumDisplayDevices(NULL, nDeviceIndex, &DispDev, 0))
        {
                hr = StringCchCopy(szSaveDeviceName, 33, DispDev.DeviceName);
                if (FAILED(hr))
                {
                // TODO: write error handler
                }

        // After second call, DispDev.DeviceString is the
        // monitor name for that device
        EnumDisplayDevices(szSaveDeviceName, 0, &DispDev, 0);

                // In the following, lpszMonitorInfo must be 128 + 1 for
                // the null-terminator.
                hr = StringCchCopy(lpszMonitorInfo, 129, DispDev.DeviceString);
                if (FAILED(hr))
                {
                // TODO: write error handler
                }

    } else    {
        bRet = FALSE;
    }

    FreeLibrary(hInstUser32);

    return bRet;
}
*/

bool TWorld::Init(HWND NhWnd, HDC hDC)
{
	auto timestart = std::chrono::system_clock::now();
    Global::hWnd = NhWnd; // do WM_COPYDATA
    Global::pCamera = &Camera; // Ra: wskaźnik potrzebny do likwidacji drgań
    Global::detonatoryOK = true;
    WriteLog("Starting MaSzyna rail vehicle simulator.");
    WriteLog(Global::asVersion);
	if( sizeof( TSubModel ) != 256 ) {
		Error( "Wrong sizeof(TSubModel) is " + std::to_string(sizeof( TSubModel )) );
		return false;
	}
    WriteLog("Online documentation and additional files on http://eu07.pl");
    WriteLog("Authors: Marcin_EU, McZapkie, ABu, Winger, Tolaris, nbmx, OLO_EU, Bart, Quark-t, "
             "ShaXbee, Oli_EU, youBy, KURS90, Ra, hunter, szociu, Stele, Q, firleju and others");
    WriteLog("Renderer:");
    WriteLog((char *)glGetString(GL_RENDERER));
    WriteLog("Vendor:");
    // Winger030405: sprawdzanie sterownikow
    WriteLog((char *)glGetString(GL_VENDOR));
    std::string glver = ((char *)glGetString(GL_VERSION));
    WriteLog("OpenGL Version:");
    WriteLog(glver);
    if ((glver == "1.5.1") || (glver == "1.5.2"))
    {
        Error("Niekompatybilna wersja openGL - dwuwymiarowy tekst nie bedzie wyswietlany!");
        WriteLog("WARNING! This OpenGL version is not fully compatible with simulator!");
        WriteLog("UWAGA! Ta wersja OpenGL nie jest w pelni kompatybilna z symulatorem!");
        Global::detonatoryOK = false;
    }
    else
        Global::detonatoryOK = true;
    // Ra: umieszczone w EU07.cpp jakoś nie chce działać
	while( glver.rfind( '.' ) > glver.find( '.' ) ) {
		glver = glver.substr( 0, glver.rfind( '.' ) - 1 ); // obcięcie od drugiej kropki
	}
    double ogl;
    try
    {
        ogl = std::stod( glver );
    }
    catch (...)
    {
        ogl = 0.0;
    }
    if (Global::fOpenGL > 0.0) // jeśli była wpisane maksymalna wersja w EU07.INI
    {
        if (ogl > 0.0) // zakładając, że się odczytało dobrze
            if (ogl < Global::fOpenGL) // a karta oferuje niższą wersję niż wpisana
                Global::fOpenGL = ogl; // to przyjąc to z karty
    }
    else if (false == GLEW_VERSION_1_3) // sprzętowa deompresja DDS zwykle wymaga 1.3
        Error("Missed OpenGL 1.3+ drivers!"); // błąd np. gdy wersja 1.1, a nie ma wpisu w EU07.INI
/*
    Global::bOpenGL_1_5 = (Global::fOpenGL >= 1.5); // są fragmentaryczne animacje VBO
*/
    WriteLog("Supported extensions:");
    WriteLog((char *)glGetString(GL_EXTENSIONS));
    if (GLEW_ARB_vertex_buffer_object) // czy jest VBO w karcie graficznej
    {
        if( std::string( (char *)glGetString(GL_VENDOR) ).find("Intel") != std::string::npos ) // wymuszenie tylko dla kart Intel
        { // karty Intel nie nadają się do grafiki 3D, ale robimy wyjątek, bo to w końcu symulator
            Global::iMultisampling =
                0; // to robi problemy na "Intel(R) HD Graphics Family" - czarny ekran
            if (Global::fOpenGL >=
                1.4) // 1.4 miało obsługę VBO, ale bez opcji modyfikacji fragmentu bufora
                Global::bUseVBO = true; // VBO włączane tylko, jeśli jest obsługa oraz nie ustawiono
            // niższego numeru
        }
        if (Global::bUseVBO)
            WriteLog("Ra: The VBO is found and will be used.");
        else
            WriteLog("Ra: The VBO is found, but Display Lists are selected.");
    }
    else
    {
        WriteLog("Ra: No VBO found - Display Lists used. Graphics card too old?");
        Global::bUseVBO = false; // może być włączone parametrem w INI
    }
    if (Global::bDecompressDDS) // jeśli sprzętowa (domyślnie jest false)
        WriteLog("DDS textures support at OpenGL level is disabled in INI file.");
    else
    {
        Global::bDecompressDDS =
            !(true == GLEW_EXT_texture_compression_s3tc); // czy obsługiwane?
        if (Global::bDecompressDDS) // czy jest obsługa DDS w karcie graficznej
            WriteLog("DDS textures are not supported.");
        else // brak obsługi DDS - trzeba włączyć programową dekompresję
            WriteLog("DDS textures are supported.");
    }
    if (Global::iMultisampling)
        WriteLog("Used multisampling of " + std::to_string(Global::iMultisampling) + " samples.");
    { // ograniczenie maksymalnego rozmiaru tekstur - parametr dla skalowania tekstur
        GLint i;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &i);
        if (i < Global::iMaxTextureSize)
            Global::iMaxTextureSize = i;
        WriteLog("Max texture size: " + std::to_string(Global::iMaxTextureSize));
    }
    /*-----------------------Render Initialization----------------------*/
    if (Global::fOpenGL >= 1.2) // poniższe nie działa w 1.1
        glTexEnvf(TEXTURE_FILTER_CONTROL_EXT, TEXTURE_LOD_BIAS_EXT, -1);
    GLfloat FogColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
    glMatrixMode(GL_MODELVIEW); // Select The Modelview Matrix
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); // Clear screen and depth buffer
    glLoadIdentity();
    // WriteLog("glClearColor (FogColor[0], FogColor[1], FogColor[2], 0.0); ");
    // glClearColor (1.0, 0.0, 0.0, 0.0);                  // Background Color
    // glClearColor (FogColor[0], FogColor[1], FogColor[2], 0.0);
	// Background    // Color
	glClearColor(Global::Background[0], Global::Background[1], Global::Background[2], 1.0); // Background Color

    WriteLog("glFogfv(GL_FOG_COLOR, FogColor);");
    glFogfv(GL_FOG_COLOR, FogColor); // Set Fog Color

    WriteLog("glClearDepth(1.0f);  ");
    glClearDepth(1.0f); // ZBuffer Value

    //  glEnable(GL_NORMALIZE);
    //    glEnable(GL_RESCALE_NORMAL);

    //    glEnable(GL_CULL_FACE);
    WriteLog("glEnable(GL_TEXTURE_2D);");
    glEnable(GL_TEXTURE_2D); // Enable Texture Mapping
    WriteLog("glShadeModel(GL_SMOOTH);");
    glShadeModel(GL_SMOOTH); // Enable Smooth Shading
    WriteLog("glEnable(GL_DEPTH_TEST);");
    glEnable(GL_DEPTH_TEST);

    // McZapkie:261102-uruchomienie polprzezroczystosci (na razie linie) pod kierunkiem Marcina
    // if (Global::bRenderAlpha) //Ra: wywalam tę flagę
    {
        WriteLog("glEnable(GL_BLEND);");
        glEnable(GL_BLEND);
        WriteLog("glEnable(GL_ALPHA_TEST);");
        glEnable(GL_ALPHA_TEST);
        WriteLog("glAlphaFunc(GL_GREATER,0.04);");
        glAlphaFunc(GL_GREATER, 0.04);
        WriteLog("glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);");
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        WriteLog("glDepthFunc(GL_LEQUAL);");
        glDepthFunc(GL_LEQUAL);
    }
    /*
        else
        {
          WriteLog("glEnable(GL_ALPHA_TEST);");
          glEnable(GL_ALPHA_TEST);
          WriteLog("glAlphaFunc(GL_GREATER,0.5);");
          glAlphaFunc(GL_GREATER,0.5);
          WriteLog("glDepthFunc(GL_LEQUAL);");
          glDepthFunc(GL_LEQUAL);
          WriteLog("glDisable(GL_BLEND);");
          glDisable(GL_BLEND);
        }
    */
    /* zakomentowanie to co bylo kiedys mieszane
        WriteLog("glEnable(GL_ALPHA_TEST);");
        glEnable(GL_ALPHA_TEST);//glGetIntegerv()
        WriteLog("glAlphaFunc(GL_GREATER,0.5);");
    //    glAlphaFunc(GL_LESS,0.5);
        glAlphaFunc(GL_GREATER,0.5);
    //    glBlendFunc(GL_SRC_ALPHA,GL_ONE);
        WriteLog("glDepthFunc(GL_LEQUAL);");
        glDepthFunc(GL_LEQUAL);//EQUAL);
    // The Type Of Depth Testing To Do
      //  glEnable(GL_BLEND);
    //    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    */

    WriteLog("glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);");
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST); // Really Nice Perspective Calculations

    WriteLog("glPolygonMode(GL_FRONT, GL_FILL);");
    glPolygonMode(GL_FRONT, GL_FILL);
    WriteLog("glFrontFace(GL_CCW);");
    glFrontFace(GL_CCW); // Counter clock-wise polygons face out
    WriteLog("glEnable(GL_CULL_FACE);	");
    glEnable(GL_CULL_FACE); // Cull back-facing triangles
    WriteLog("glLineWidth(1.0f);");
    glLineWidth(1.0f);
    //	glLineWidth(2.0f);
    WriteLog("glPointSize(2.0f);");
    glPointSize(2.0f);

    // ----------- LIGHTING SETUP -----------
    // Light values and coordinates

    vector3 lp = Normalize(vector3(-500, 500, 200));

    Global::lightPos[0] = lp.x;
    Global::lightPos[1] = lp.y;
    Global::lightPos[2] = lp.z;
    Global::lightPos[3] = 0.0f;

    // Ra: światła by sensowniej było ustawiać po wczytaniu scenerii

    // Ra: szczątkowe światło rozproszone - żeby było cokolwiek widać w ciemności
    WriteLog("glLightModelfv(GL_LIGHT_MODEL_AMBIENT,darkLight);");
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, Global::darkLight);

    // Ra: światło 0 - główne światło zewnętrzne (Słońce, Księżyc)
    WriteLog("glLightfv(GL_LIGHT0,GL_AMBIENT,ambientLight);");
    glLightfv(GL_LIGHT0, GL_AMBIENT, Global::ambientDayLight);
    WriteLog("glLightfv(GL_LIGHT0,GL_DIFFUSE,diffuseLight);");
    glLightfv(GL_LIGHT0, GL_DIFFUSE, Global::diffuseDayLight);
    WriteLog("glLightfv(GL_LIGHT0,GL_SPECULAR,specularLight);");
    glLightfv(GL_LIGHT0, GL_SPECULAR, Global::specularDayLight);
    WriteLog("glLightfv(GL_LIGHT0,GL_POSITION,lightPos);");
    glLightfv(GL_LIGHT0, GL_POSITION, Global::lightPos);
    WriteLog("glEnable(GL_LIGHT0);");
    glEnable(GL_LIGHT0);

    // glColor() ma zmieniać kolor wybrany w glColorMaterial()
    WriteLog("glEnable(GL_COLOR_MATERIAL);");
    glEnable(GL_COLOR_MATERIAL);

    WriteLog("glColorMaterial(GL_FRONT, GL_AMBIENT_AND_DIFFUSE);");
    glColorMaterial(GL_FRONT, GL_AMBIENT_AND_DIFFUSE);

    //    WriteLog("glMaterialfv( GL_FRONT, GL_AMBIENT, whiteLight );");
    //	glMaterialfv( GL_FRONT, GL_AMBIENT, Global::whiteLight );

    WriteLog("glMaterialfv( GL_FRONT, GL_AMBIENT_AND_DIFFUSE, whiteLight );");
    glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, Global::whiteLight);

    /*
        WriteLog("glMaterialfv( GL_FRONT, GL_SPECULAR, noLight );");
            glMaterialfv( GL_FRONT, GL_SPECULAR, Global::noLight );
    */

    WriteLog("glEnable(GL_LIGHTING);");
    glEnable(GL_LIGHTING);

    WriteLog("glFogi(GL_FOG_MODE, GL_LINEAR);");
    glFogi(GL_FOG_MODE, GL_LINEAR); // Fog Mode
    WriteLog("glFogfv(GL_FOG_COLOR, FogColor);");
    glFogfv(GL_FOG_COLOR, FogColor); // Set Fog Color
    //	glFogf(GL_FOG_DENSITY, 0.594f);						// How Dense Will The
    //Fog
    // Be
    //	glHint(GL_FOG_HINT, GL_NICEST);					    // Fog Hint Value
    WriteLog("glFogf(GL_FOG_START, 1000.0f);");
    glFogf(GL_FOG_START, 10.0f); // Fog Start Depth
    WriteLog("glFogf(GL_FOG_END, 2000.0f);");
    glFogf(GL_FOG_END, 200.0f); // Fog End Depth
    WriteLog("glEnable(GL_FOG);");
    glEnable(GL_FOG); // Enables GL_FOG

    // Ra: ustawienia testowe
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
    glHint(GL_POLYGON_SMOOTH_HINT, GL_NICEST);

    /*--------------------Render Initialization End---------------------*/

    WriteLog("Font init"); // początek inicjacji fontów 2D
    if (Global::bGlutFont) // jeśli wybrano GLUT font, próbujemy zlinkować GLUT32.DLL
    {
        UINT mode = SetErrorMode(SEM_NOOPENFILEERRORBOX); // aby nie wrzeszczał, że znaleźć nie może
        hinstGLUT32 = LoadLibrary(TEXT("GLUT32.DLL")); // get a handle to the DLL module
        SetErrorMode(mode);
        // If the handle is valid, try to get the function address.
        if (hinstGLUT32)
            glutBitmapCharacterDLL =
                (FglutBitmapCharacter)GetProcAddress(hinstGLUT32, "glutBitmapCharacter");
        else
            WriteLog("Missed GLUT32.DLL.");
        if (glutBitmapCharacterDLL)
            WriteLog("Used font from GLUT32.DLL.");
        else
            Global::bGlutFont = false; // nie udało się, trzeba spróbować na Display List
    }
    if (!Global::bGlutFont)
    { // jeśli bezGLUTowy font
        HFONT font; // Windows Font ID
        base = glGenLists(96); // storage for 96 characters
        font = CreateFont(-15, // height of font
                          0, // width of font
                          0, // angle of escapement
                          0, // orientation angle
                          FW_BOLD, // font weight
                          FALSE, // italic
                          FALSE, // underline
                          FALSE, // strikeout
                          ANSI_CHARSET, // character set identifier
                          OUT_TT_PRECIS, // output precision
                          CLIP_DEFAULT_PRECIS, // clipping precision
                          ANTIALIASED_QUALITY, // output quality
                          FF_DONTCARE | DEFAULT_PITCH, // family and pitch
                          "Courier New"); // font name
        SelectObject(hDC, font); // selects the font we want
        wglUseFontBitmapsA(hDC, 32, 96, base); // builds 96 characters starting at character 32
        WriteLog("Display Lists font used."); //+AnsiString(glGetError())
    }
    WriteLog("Font init OK"); //+AnsiString(glGetError())

    Timer::ResetTimers();

    hWnd = NhWnd;
    glColor4f(1.0f, 3.0f, 3.0f, 0.0f);
    //    SwapBuffers(hDC);					// Swap Buffers (Double Buffering)
    //    glClear(GL_COLOR_BUFFER_BIT);
    //    glFlush();

    SetForegroundWindow(hWnd);
    WriteLog("Sound Init");

    glLoadIdentity();
    //    glColor4f(0.3f,0.0f,0.0f,0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glTranslatef(0.0f, 0.0f, -0.50f);
    //    glRasterPos2f(-0.25f, -0.10f);
    glDisable(GL_DEPTH_TEST); // Disables depth testing
    glColor3f(3.0f, 3.0f, 3.0f);

    GLuint logo;
    logo = TTexturesManager::GetTextureID(szTexturePath, szSceneryPath, "logo", 6);
    glBindTexture(GL_TEXTURE_2D, logo); // Select our texture

    glBegin(GL_QUADS); // Drawing using triangles
    glTexCoord2f(0.0f, 0.0f);
    glVertex3f(-0.28f, -0.22f, 0.0f); // bottom left of the texture and quad
    glTexCoord2f(1.0f, 0.0f);
    glVertex3f(0.28f, -0.22f, 0.0f); // bottom right of the texture and quad
    glTexCoord2f(1.0f, 1.0f);
    glVertex3f(0.28f, 0.22f, 0.0f); // top right of the texture and quad
    glTexCoord2f(0.0f, 1.0f);
    glVertex3f(-0.28f, 0.22f, 0.0f); // top left of the texture and quad
    glEnd();
    //~logo; Ra: to jest bez sensu zapis
    glColor3f(0.0f, 0.0f, 100.0f);
    if (Global::detonatoryOK)
    {
        glRasterPos2f(-0.25f, -0.09f);
        glPrint("Uruchamianie / Initializing...");
        glRasterPos2f(-0.25f, -0.10f);
        glPrint("Dzwiek / Sound...");
    }
    SwapBuffers(hDC); // Swap Buffers (Double Buffering)

    glEnable(GL_LIGHTING);
    /*-----------------------Sound Initialization-----------------------*/
    TSoundsManager::Init(hWnd);
    // TSoundsManager::LoadSounds( "" );
    /*---------------------Sound Initialization End---------------------*/
    WriteLog("Sound Init OK");
    if (Global::detonatoryOK)
    {
        glRasterPos2f(-0.25f, -0.11f);
        glPrint("OK.");
    }
    SwapBuffers(hDC); // Swap Buffers (Double Buffering)

    int i;

    Paused = true;
    WriteLog("Textures init");
    if (Global::detonatoryOK)
    {
        glRasterPos2f(-0.25f, -0.12f);
        glPrint("Tekstury / Textures...");
    }
    SwapBuffers(hDC); // Swap Buffers (Double Buffering)

    TTexturesManager::Init();
    WriteLog("Textures init OK");
    if (Global::detonatoryOK)
    {
        glRasterPos2f(-0.25f, -0.13f);
        glPrint("OK.");
    }
    SwapBuffers(hDC); // Swap Buffers (Double Buffering)

    WriteLog("Models init");
    if (Global::detonatoryOK)
    {
        glRasterPos2f(-0.25f, -0.14f);
        glPrint("Modele / Models...");
    }
    SwapBuffers(hDC); // Swap Buffers (Double Buffering)
    // McZapkie: dodalem sciezke zeby mozna bylo definiowac skad brac modele ale to malo eleganckie
    //    TModelsManager::LoadModels(asModelsPatch);
    TModelsManager::Init();
    WriteLog("Models init OK");
    if (Global::detonatoryOK)
    {
        glRasterPos2f(-0.25f, -0.15f);
        glPrint("OK.");
    }
    SwapBuffers(hDC); // Swap Buffers (Double Buffering)

    WriteLog("Ground init");
    if (Global::detonatoryOK)
    {
        glRasterPos2f(-0.25f, -0.16f);
        glPrint("Sceneria / Scenery (please wait)...");
    }
    SwapBuffers(hDC); // Swap Buffers (Double Buffering)

    Ground.Init(Global::SceneryFile, hDC);
    //    Global::tSinceStart= 0;
    Clouds.Init();
    WriteLog("Ground init OK");
    if (Global::detonatoryOK)
    {
        glRasterPos2f(-0.25f, -0.17f);
        glPrint("OK.");
    }
    SwapBuffers(hDC); // Swap Buffers (Double Buffering)

    //    TTrack *Track=Ground.FindGroundNode("train_start",TP_TRACK)->pTrack;

    //    Camera.Init(vector3(2700,10,6500),0,M_PI,0);
    //    Camera.Init(vector3(00,40,000),0,M_PI,0);
    //    Camera.Init(vector3(1500,5,-4000),0,M_PI,0);
    // McZapkie-130302 - coby nie przekompilowywac:
    //      Camera.Init(Global::pFreeCameraInit,0,M_PI,0);
    Camera.Init(Global::pFreeCameraInit[0], Global::pFreeCameraInitAngle[0]);

    char buff[255] = "Player train init: ";
    if (Global::detonatoryOK)
    {
        glRasterPos2f(-0.25f, -0.18f);
        glPrint("Przygotowanie kabiny do sterowania...");
    }
    SwapBuffers(hDC); // Swap Buffers (Double Buffering)

    strcat(buff, Global::asHumanCtrlVehicle.c_str());
    WriteLog(buff);
    TGroundNode *nPlayerTrain = NULL;
    if (Global::asHumanCtrlVehicle != "ghostview")
        nPlayerTrain = Ground.DynamicFind(Global::asHumanCtrlVehicle); // szukanie w tych z obsadą
    if (nPlayerTrain)
    {
        Train = new TTrain();
        if (Train->Init(nPlayerTrain->DynamicObject))
        {
            Controlled = Train->Dynamic();
            mvControlled = Controlled->ControlledFind()->MoverParameters;
            Global::pUserDynamic = Controlled; // renerowanie pojazdu względem kabiny
            WriteLog("Player train init OK");
            if (Global::detonatoryOK)
            {
                glRasterPos2f(-0.25f, -0.19f);
                glPrint("OK.");
            }
            FollowView();
            SwapBuffers(hDC); // Swap Buffers (Double Buffering)
        }
        else
        {
            Error("Player train init failed!");
            FreeFlyModeFlag = true; // Ra: automatycznie włączone latanie
            if (Global::detonatoryOK)
            {
                glRasterPos2f(-0.25f, -0.20f);
                glPrint("Blad inicjalizacji sterowanego pojazdu!");
            }
            SwapBuffers(hDC); // Swap Buffers (Double Buffering)
            Controlled = NULL;
            mvControlled = NULL;
            Camera.Type = tp_Free;
        }
    }
    else
    {
        if (Global::asHumanCtrlVehicle != "ghostview")
        {
            Error("Player train not exist!");
            if (Global::detonatoryOK)
            {
                glRasterPos2f(-0.25f, -0.20f);
                glPrint("Wybrany pojazd nie istnieje w scenerii!");
            }
        }
        FreeFlyModeFlag = true; // Ra: automatycznie włączone latanie
        SwapBuffers(hDC); // swap buffers (double buffering)
        Controlled = NULL;
        mvControlled = NULL;
        Camera.Type = tp_Free;
    }
    glEnable(GL_DEPTH_TEST);
    // Ground.pTrain=Train;
    // if (!Global::bMultiplayer) //na razie włączone
    { // eventy aktywowane z klawiatury tylko dla jednego użytkownika
        KeyEvents[0] = Ground.FindEvent("keyctrl00");
        KeyEvents[1] = Ground.FindEvent("keyctrl01");
        KeyEvents[2] = Ground.FindEvent("keyctrl02");
        KeyEvents[3] = Ground.FindEvent("keyctrl03");
        KeyEvents[4] = Ground.FindEvent("keyctrl04");
        KeyEvents[5] = Ground.FindEvent("keyctrl05");
        KeyEvents[6] = Ground.FindEvent("keyctrl06");
        KeyEvents[7] = Ground.FindEvent("keyctrl07");
        KeyEvents[8] = Ground.FindEvent("keyctrl08");
        KeyEvents[9] = Ground.FindEvent("keyctrl09");
    }
    // glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);  //{Texture blends with object
    // background}
	if (Global::bOldSmudge == true)
	light = TTexturesManager::GetTextureID(szTexturePath, szSceneryPath, "smuga.tga");
	else
    light = TTexturesManager::GetTextureID(szTexturePath, szSceneryPath, "smuga2.tga");
    // Camera.Reset();
    Timer::ResetTimers();
	WriteLog( "Load time: " +
		std::to_string( std::chrono::duration_cast<std::chrono::seconds>(( std::chrono::system_clock::now() - timestart )).count() )
		+ " seconds");
    if (DebugModeFlag) // w Debugmode automatyczne włączenie AI
        if (Train)
            if (Train->Dynamic()->Mechanik)
                Train->Dynamic()->Mechanik->TakeControl(true);
    return true;
};

void TWorld::OnKeyDown(int cKey)
{ //(cKey) to kod klawisza, cyfrowe i literowe się zgadzają
    // Ra 2014-09: tu by można dodać tabelę konwersji: 256 wirtualnych kodów w kontekście dwóch
    // przełączników [Shift] i [Ctrl]
    // na każdy kod wirtualny niech przypadają 4 bajty: 2 dla naciśnięcia i 2 dla zwolnienia
    // powtórzone 256 razy da 1kB na każdy stan przełączników, łącznie będzie 4kB pierwszej tabeli
    // przekodowania
    if (!Global::iPause)
    { // podczas pauzy klawisze nie działają
        std::string info = "Key pressed: [";
        if (Console::Pressed(VK_SHIFT))
            info += "Shift]+[";
        if (Console::Pressed(VK_CONTROL))
            info += "Ctrl]+[";
        if (cKey > 192) // coś tam jeszcze ciekawego jest?
        {
            if (cKey < 255) // 255 to [Fn] w laptopach
                WriteLog(info + (char)(cKey - 128) + "]");
        }
        else if (cKey >= 186)
            WriteLog(info + std::string(";=,-./~").substr(cKey - 186, 1) + "]");
        else if (cKey > 123) // coś tam jeszcze ciekawego jest?
            WriteLog(info + std::to_string(cKey) + "]"); // numer klawisza
        else if (cKey >= 112) // funkcyjne
            WriteLog(info + "F" + std::to_string(cKey - 111) + "]");
        else if (cKey >= 96)
            WriteLog(info + "Num" + std::string("0123456789*+?-./").substr(cKey - 96, 1) + "]");
        else if (((cKey >= '0') && (cKey <= '9')) || ((cKey >= 'A') && (cKey <= 'Z')) ||
                 (cKey == ' '))
            WriteLog(info + (char)(cKey) + "]");
        else if (cKey == '-')
            WriteLog(info + "Insert]");
        else if (cKey == '.')
            WriteLog(info + "Delete]");
        else if (cKey == '$')
            WriteLog(info + "Home]");
        else if (cKey == '#')
            WriteLog(info + "End]");
        else if (cKey > 'Z') //żeby nie logować kursorów
            WriteLog(info + std::to_string(cKey) + "]"); // numer klawisza
    }
    if ((cKey <= '9') ? (cKey >= '0') : false) // klawisze cyfrowe
    {
        int i = cKey - '0'; // numer klawisza
        if (Console::Pressed(VK_SHIFT))
        { // z [Shift] uruchomienie eventu
            if (!Global::iPause) // podczas pauzy klawisze nie działają
                if (KeyEvents[i])
                    Ground.AddToQuery(KeyEvents[i], NULL);
        }
        else // zapamiętywanie kamery może działać podczas pauzy
            if (FreeFlyModeFlag) // w trybie latania można przeskakiwać do ustawionych kamer
            if ((Global::iTextMode != VK_F12) &&
                (Global::iTextMode != VK_F3)) // ograniczamy użycie kamer
            {
                if ((!Global::pFreeCameraInit[i].x && !Global::pFreeCameraInit[i].y &&
                     !Global::pFreeCameraInit[i].z))
                { // jeśli kamera jest w punkcie zerowym, zapamiętanie współrzędnych i kątów
                    Global::pFreeCameraInit[i] = Camera.Pos;
                    Global::pFreeCameraInitAngle[i].x = Camera.Pitch;
                    Global::pFreeCameraInitAngle[i].y = Camera.Yaw;
                    Global::pFreeCameraInitAngle[i].z = Camera.Roll;
                    // logowanie, żeby można było do scenerii przepisać
                    WriteLog(
                        "camera " + std::to_string( Global::pFreeCameraInit[i].x ) + " " +
                        std::to_string(Global::pFreeCameraInit[i].y ) + " " +
                        std::to_string(Global::pFreeCameraInit[i].z ) + " " +
                        std::to_string(RadToDeg(Global::pFreeCameraInitAngle[i].x)) +
                        " " +
                        std::to_string(RadToDeg(Global::pFreeCameraInitAngle[i].y)) +
                        " " +
                        std::to_string(RadToDeg(Global::pFreeCameraInitAngle[i].z)) +
                        " " + std::to_string(i) + " endcamera");
                }
                else // również przeskakiwanie
                { // Ra: to z tą kamerą (Camera.Pos i Global::pCameraPosition) jest trochę bez sensu
                    Global::SetCameraPosition(
                        Global::pFreeCameraInit[i]); // nowa pozycja dla generowania obiektów
                    Ground.Silence(Camera.Pos); // wyciszenie wszystkiego z poprzedniej pozycji
                    Camera.Init(Global::pFreeCameraInit[i],
                                Global::pFreeCameraInitAngle[i]); // przestawienie
                }
            }
        // będzie jeszcze załączanie sprzęgów z [Ctrl]
    }
    else if ((cKey >= VK_F1) ? (cKey <= VK_F12) : false)
    {
        switch (cKey)
        {
        case VK_F1: // czas i relacja
        case VK_F3:
        case VK_F5: // przesiadka do innego pojazdu
        case VK_F8: // FPS
        case VK_F9: // wersja, typ wyświetlania, błędy OpenGL
        case VK_F10:
            if (Global::iTextMode == cKey)
                Global::iTextMode =
                    (Global::iPause && (cKey != VK_F1) ? VK_F1 :
                                                         0); // wyłączenie napisów, chyba że pauza
            else
                Global::iTextMode = cKey;
            break;
        case VK_F2: // parametry pojazdu
            if (Global::iTextMode == cKey) // jeśli kolejne naciśnięcie
                ++Global::iScreenMode[cKey - VK_F1]; // kolejny ekran
            else
            { // pierwsze naciśnięcie daje pierwszy (tzn. zerowy) ekran
                Global::iTextMode = cKey;
                Global::iScreenMode[cKey - VK_F1] = 0;
            }
            break;
        case VK_F12: // coś tam jeszcze
            if (Console::Pressed(VK_CONTROL) && Console::Pressed(VK_SHIFT))
                DebugModeFlag = !DebugModeFlag; // taka opcjonalna funkcja, może się czasem przydać
            /* //Ra 2F1P: teraz włączanie i wyłączanie klawiszami cyfrowymi po użyciu [F12]
                else if (Console::Pressed(VK_SHIFT))
                {//odpalenie logu w razie "W"
                 if ((Global::iWriteLogEnabled&2)==0) //nie było okienka
                 {//otwarcie okna
                  AllocConsole();
                  SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE),FOREGROUND_GREEN);
                 }
                 Global::iWriteLogEnabled|=3;
                } */
            else
                Global::iTextMode = cKey;
            break;
        case VK_F4:
            InOutKey();
            break;
        case VK_F6:
            if (DebugModeFlag)
            { // przyspieszenie symulacji do testowania scenerii... uwaga na FPS!
                // Global::iViewMode=VK_F6;
                if (Console::Pressed(VK_CONTROL))
                    Global::fTimeSpeed = (Console::Pressed(VK_SHIFT) ? 10.0 : 5.0);
                else
                    Global::fTimeSpeed = (Console::Pressed(VK_SHIFT) ? 2.0 : 1.0);
            }
            break;
        }
        // if (cKey!=VK_F4)
        return; // nie są przekazywane do pojazdu wcale
    }
    if (Global::iTextMode == VK_F10) // wyświetlone napisy klawiszem F10
    { // i potwierdzenie
        Global::iTextMode = (cKey == 'Y') ? -1 : 0; // flaga wyjścia z programu
        return; // nie przekazujemy do pociągu
    }
    else if ((Global::iTextMode == VK_F12) ? (cKey >= '0') && (cKey <= '9') : false)
    { // tryb konfiguracji debugmode (przestawianie kamery już wyłączone
        if (!Console::Pressed(VK_SHIFT)) // bez [Shift]
        {
            if (cKey == '1')
                Global::iWriteLogEnabled ^= 1; // włącz/wyłącz logowanie do pliku
            else if (cKey == '2')
            { // włącz/wyłącz okno konsoli
                Global::iWriteLogEnabled ^= 2;
                if ((Global::iWriteLogEnabled & 2) == 0) // nie było okienka
                { // otwarcie okna
                    AllocConsole(); // jeśli konsola już jest, to zwróci błąd; uwalniać nie ma po
                    // co, bo się odłączy
                    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_GREEN);
                }
            }
            // else if (cKey=='3') Global::iWriteLogEnabled^=4; //wypisywanie nazw torów
        }
    }
    else if (cKey == 3) //[Ctrl]+[Break]
    { // hamowanie wszystkich pojazdów w okolicy
		if (Controlled->MoverParameters->Radio)
			Ground.RadioStop(Camera.Pos);
	}
    else if (!Global::iPause) //||(cKey==VK_F4)) //podczas pauzy sterownaie nie działa, F4 tak
        if (Train)
            if (Controlled)
                if ((Controlled->Controller == Humandriver) ? true : DebugModeFlag || (cKey == 'Q'))
                    Train->OnKeyDown(cKey); // przekazanie klawisza do kabiny
    if (FreeFlyModeFlag) // aby nie odluźniało wagonu za lokomotywą
    { // operacje wykonywane na dowolnym pojeździe, przeniesione tu z kabiny
        if (cKey == Global::Keys[k_Releaser]) // odluźniacz
        { // działa globalnie, sprawdzić zasięg
            TDynamicObject *temp = Global::DynamicNearest();
            if (temp)
            {
                if (GetAsyncKeyState(VK_CONTROL) < 0) // z ctrl odcinanie
                {
                    temp->MoverParameters->BrakeStatus ^= 128;
                }
                else if (temp->MoverParameters->BrakeReleaser(1))
                {
                    // temp->sBrakeAcc->
                    // dsbPneumaticRelay->SetVolume(DSBVOLUME_MAX);
                    // dsbPneumaticRelay->Play(0,0,0); //temp->Position()-Camera.Pos //???
                }
            }
        }
        else if (cKey == Global::Keys[k_Heating]) // Ra: klawisz nie jest najszczęśliwszy
        { // zmiana próżny/ładowny; Ra: zabrane z kabiny
            TDynamicObject *temp = Global::DynamicNearest();
            if (temp)
            {
                if (Console::Pressed(VK_SHIFT) ? temp->MoverParameters->IncBrakeMult() :
                                                 temp->MoverParameters->DecBrakeMult())
                    if (Train)
                    { // dźwięk oczywiście jest w kabinie
                        Train->dsbSwitch->SetVolume(DSBVOLUME_MAX);
                        Train->dsbSwitch->Play(0, 0, 0);
                    }
            }
        }
        else if (cKey == Global::Keys[k_EndSign])
        { // Ra 2014-07: zabrane z kabiny
            TDynamicObject *tmp = Global::CouplerNearest(); // domyślnie wyszukuje do 20m
            if (tmp)
            {
                int CouplNr = (LengthSquared3(tmp->HeadPosition() - Camera.Pos) >
                                       LengthSquared3(tmp->RearPosition() - Camera.Pos) ?
                                   1 :
                                   -1) *
                              tmp->DirectionGet();
                if (CouplNr < 0)
                    CouplNr = 0; // z [-1,1] zrobić [0,1]
                int mask, set = 0; // Ra: [Shift]+[Ctrl]+[T] odpala mi jakąś idiotyczną zmianę
                // tapety pulpitu :/
                if (GetAsyncKeyState(VK_SHIFT) < 0) // z [Shift] zapalanie
                    set = mask = 64; // bez [Ctrl] założyć tabliczki
                else if (GetAsyncKeyState(VK_CONTROL) < 0)
                    set = mask = 2 + 32; // z [Ctrl] zapalić światła czerwone
                else
                    mask = 2 + 32 + 64; // wyłączanie ściąga wszystko
                if (((tmp->iLights[CouplNr]) & mask) != set)
                {
                    tmp->iLights[CouplNr] = (tmp->iLights[CouplNr] & ~mask) | set;
                    if (Train)
                    { // Ra: ten dźwięk z kabiny to przegięcie, ale na razie zostawiam
                        Train->dsbSwitch->SetVolume(DSBVOLUME_MAX);
                        Train->dsbSwitch->Play(0, 0, 0);
                    }
                }
            }
        }
        else if (cKey == Global::Keys[k_IncLocalBrakeLevel])
        { // zahamowanie dowolnego pojazdu
            TDynamicObject *temp = Global::DynamicNearest();
            if (temp)
            {
                if (GetAsyncKeyState(VK_CONTROL) < 0)
                    if ((temp->MoverParameters->LocalBrake == ManualBrake) ||
                        (temp->MoverParameters->MBrake == true))
                        temp->MoverParameters->IncManualBrakeLevel(1);
                    else
                        ;
                else if (temp->MoverParameters->LocalBrake != ManualBrake)
                    if (temp->MoverParameters->IncLocalBrakeLevelFAST())
                        if (Train)
                        { // dźwięk oczywiście jest w kabinie
                            Train->dsbPneumaticRelay->SetVolume(-80);
                            Train->dsbPneumaticRelay->Play(0, 0, 0);
                        }
            }
        }
        else if (cKey == Global::Keys[k_DecLocalBrakeLevel])
        { // odhamowanie dowolnego pojazdu
            TDynamicObject *temp = Global::DynamicNearest();
            if (temp)
            {
                if (GetAsyncKeyState(VK_CONTROL) < 0)
                    if ((temp->MoverParameters->LocalBrake == ManualBrake) ||
                        (temp->MoverParameters->MBrake == true))
                        temp->MoverParameters->DecManualBrakeLevel(1);
                    else
                        ;
                else if (temp->MoverParameters->LocalBrake != ManualBrake)
                    if (temp->MoverParameters->DecLocalBrakeLevelFAST())
                        if (Train)
                        { // dźwięk oczywiście jest w kabinie
                            Train->dsbPneumaticRelay->SetVolume(-80);
                            Train->dsbPneumaticRelay->Play(0, 0, 0);
                        }
            }
        }
    }
    // switch (cKey)
    //{case 'a': //ignorowanie repetycji
    // case 'A': Global::iKeyLast=cKey; break;
    // default: Global::iKeyLast=0;
    //}
}

void TWorld::OnKeyUp(int cKey)
{ // zwolnienie klawisza; (cKey) to kod klawisza, cyfrowe i literowe się zgadzają
    if (!Global::iPause) // podczas pauzy sterownaie nie działa
        if (Train)
            if (Controlled)
                if ((Controlled->Controller == Humandriver) ? true : DebugModeFlag || (cKey == 'Q'))
                    Train->OnKeyUp(cKey); // przekazanie zwolnienia klawisza do kabiny
};

void TWorld::OnMouseMove(double x, double y)
{ // McZapkie:060503-definicja obracania myszy
    Camera.OnCursorMove(x * Global::fMouseXScale, -y * Global::fMouseYScale);
}

void TWorld::InOutKey()
{ // przełączenie widoku z kabiny na zewnętrzny i odwrotnie
    FreeFlyModeFlag = !FreeFlyModeFlag; // zmiana widoku
    if (FreeFlyModeFlag)
    { // jeżeli poza kabiną, przestawiamy w jej okolicę - OK
        Global::pUserDynamic = NULL; // bez renderowania względem kamery
        if (Train)
        { // Train->Dynamic()->ABuSetModelShake(vector3(0,0,0));
            Train->Silence(); // wyłączenie dźwięków kabiny
            Train->Dynamic()->bDisplayCab = false;
            DistantView();
        }
    }
    else
    { // jazda w kabinie
        if (Train)
        {
            Global::pUserDynamic = Controlled; // renerowanie względem kamery
            Train->Dynamic()->bDisplayCab = true;
            Train->Dynamic()->ABuSetModelShake(
                vector3(0, 0, 0)); // zerowanie przesunięcia przed powrotem?
            // Camera.Stop(); //zatrzymanie ruchu
            Train->MechStop();
            FollowView(); // na pozycję mecha
        }
        else
            FreeFlyModeFlag = true; // nadal poza kabiną
    }
};

void TWorld::DistantView()
{ // ustawienie widoku pojazdu z zewnątrz
    if (Controlled) // jest pojazd do prowadzenia?
    { // na prowadzony
        Camera.Pos =
            Controlled->GetPosition() +
            (Controlled->MoverParameters->ActiveCab >= 0 ? 30 : -30) * Controlled->VectorFront() +
            vector3(0, 5, 0);
        Camera.LookAt = Controlled->GetPosition();
        Camera.RaLook(); // jednorazowe przestawienie kamery
    }
    else if (pDynamicNearest) // jeśli jest pojazd wykryty blisko
    { // patrzenie na najbliższy pojazd
        Camera.Pos = pDynamicNearest->GetPosition() +
                     (pDynamicNearest->MoverParameters->ActiveCab >= 0 ? 30 : -30) *
                         pDynamicNearest->VectorFront() +
                     vector3(0, 5, 0);
        Camera.LookAt = pDynamicNearest->GetPosition();
        Camera.RaLook(); // jednorazowe przestawienie kamery
    }
};

void TWorld::FollowView(bool wycisz)
{ // ustawienie śledzenia pojazdu
    // ABu 180404 powrot mechanika na siedzenie albo w okolicę pojazdu
    // if (Console::Pressed(VK_F4)) Global::iViewMode=VK_F4;
    // Ra: na zewnątrz wychodzimy w Train.cpp
    Camera.Reset(); // likwidacja obrotów - patrzy horyzontalnie na południe
    if (Controlled) // jest pojazd do prowadzenia?
    {
        vector3 camStara =
            Camera.Pos; // przestawianie kamery jest bez sensu: do przerobienia na potem
        // Controlled->ABuSetModelShake(vector3(0,0,0));
        if (FreeFlyModeFlag)
        { // jeżeli poza kabiną, przestawiamy w jej okolicę - OK
            if (Train)
                Train->Dynamic()->ABuSetModelShake(
                    vector3(0, 0, 0)); // wyłączenie trzęsienia na siłę?
            // Camera.Pos=Train->pMechPosition+Normalize(Train->GetDirection())*20;
            DistantView(); // przestawienie kamery
            //żeby nie bylo numerów z 'fruwajacym' lokiem - konsekwencja bujania pudła
            Global::SetCameraPosition(
                Camera.Pos); // tu ustawić nową, bo od niej liczą się odległości
            Ground.Silence(camStara); // wyciszenie dźwięków z poprzedniej pozycji
        }
        else if (Train)
        { // korekcja ustawienia w kabinie - OK
            vector3 camStara =
                Camera.Pos; // przestawianie kamery jest bez sensu: do przerobienia na potem
            // Ra: czy to tu jest potrzebne, bo przelicza się kawałek dalej?
            Camera.Pos = Train->pMechPosition; // Train.GetPosition1();
            Camera.Roll = atan(Train->pMechShake.x * Train->fMechRoll); // hustanie kamery na boki
            Camera.Pitch -=
                atan(Train->vMechVelocity.z * Train->fMechPitch); // hustanie kamery przod tyl
            if (Train->Dynamic()->MoverParameters->ActiveCab == 0)
                Camera.LookAt = Train->pMechPosition + Train->GetDirection();
            else // patrz w strone wlasciwej kabiny
                Camera.LookAt =
                    Train->pMechPosition +
                    Train->GetDirection() * Train->Dynamic()->MoverParameters->ActiveCab;
            Train->pMechOffset.x = Train->pMechSittingPosition.x;
            Train->pMechOffset.y = Train->pMechSittingPosition.y;
            Train->pMechOffset.z = Train->pMechSittingPosition.z;
            Global::SetCameraPosition(
                Train->Dynamic()
                    ->GetPosition()); // tu ustawić nową, bo od niej liczą się odległości
            if (wycisz) // trzymanie prawego w kabinie daje marny efekt
                Ground.Silence(camStara); // wyciszenie dźwięków z poprzedniej pozycji
        }
    }
    else
        DistantView();
};

bool TWorld::Update()
{
#ifdef USE_SCENERY_MOVING
    vector3 tmpvector = Global::GetCameraPosition();
    tmpvector = vector3(-int(tmpvector.x) + int(tmpvector.x) % 10000,
                        -int(tmpvector.y) + int(tmpvector.y) % 10000,
                        -int(tmpvector.z) + int(tmpvector.z) % 10000);
    if (tmpvector.x || tmpvector.y || tmpvector.z)
    {
        WriteLog("Moving scenery");
        Ground.MoveGroundNode(tmpvector);
        WriteLog("Scenery moved");
    };
#endif
    if (iCheckFPS)
        --iCheckFPS;
    else
    { // jak doszło do zera, to sprawdzamy wydajność
        if (Timer::GetFPS() < Global::fFpsMin)
        {
            Global::iSegmentsRendered -=
                Random(10); // floor(0.5+Global::iSegmentsRendered/Global::fRadiusFactor);
            if (Global::iSegmentsRendered < 10) // jeśli jest co zmniejszać
                Global::iSegmentsRendered = 10; // 10=minimalny promień to 600m
        }
        else if (Timer::GetFPS() > Global::fFpsMax) // jeśli jest dużo FPS
            if (Global::iSegmentsRendered < Global::iFpsRadiusMax) // jeśli jest co zwiększać
            {
                Global::iSegmentsRendered +=
                    Random(5); // floor(0.5+Global::iSegmentsRendered*Global::fRadiusFactor);
                if (Global::iSegmentsRendered > Global::iFpsRadiusMax) // 5.6km (22*22*M_PI)
                    Global::iSegmentsRendered = Global::iFpsRadiusMax;
            }
        if ((Timer::GetFPS() < 12) && (Global::iSlowMotion < 7))
        {
            Global::iSlowMotion = (Global::iSlowMotion << 1) + 1; // zapalenie kolejnego bitu
            if (Global::iSlowMotionMask & 1)
                if (Global::iMultisampling) // a multisampling jest włączony
                    glDisable(GL_MULTISAMPLE); // wyłączenie multisamplingu powinno poprawić FPS
        }
        else if ((Timer::GetFPS() > 20) && Global::iSlowMotion)
        { // FPS się zwiększył, można włączyć bajery
            Global::iSlowMotion = (Global::iSlowMotion >> 1); // zgaszenie bitu
            if (Global::iSlowMotion == 0) // jeśli jest pełna prędkość
                if (Global::iMultisampling) // a multisampling jest włączony
                    glEnable(GL_MULTISAMPLE);
        }
        /*
          if (!Global::bPause)
           if (GetFPS()<=5)
           {//zwiększenie kroku fizyki przy słabym FPS
            if (fMaxDt<0.05)
            {fMaxDt=0.05; //Ra: tak nie może być, bo są problemy na sprzęgach
             WriteLog("Phisics step switched to 0.05s!");
            }
           }
           else if (GetFPS()>12)
            if (fMaxDt>0.01)
            {//powrót do podstawowego kroku fizyki
             fMaxDt=0.01;
             WriteLog("Phisics step switched to 0.01s!");
            }
        */
        iCheckFPS = 0.25 * Timer::GetFPS(); // tak za 0.25 sekundy sprawdzić ponownie (jeszcze przycina?)
    }
    Timer::UpdateTimers(Global::iPause);
    if (!Global::iPause)
    { // jak pauza, to nie ma po co tego przeliczać
        GlobalTime->UpdateMTableTime(Timer::GetDeltaTime()); // McZapkie-300302: czas rozkladowy
        // Ra 2014-07: przeliczenie kąta czasu (do animacji zależnych od czasu)
        Global::fTimeAngleDeg =
            GlobalTime->hh * 15.0 + GlobalTime->mm * 0.25 + GlobalTime->mr / 240.0;
        Global::fClockAngleDeg[0] = 36.0 * (int(GlobalTime->mr) % 10); // jednostki sekund
        Global::fClockAngleDeg[1] = 36.0 * (int(GlobalTime->mr) / 10); // dziesiątki sekund
        Global::fClockAngleDeg[2] = 36.0 * (GlobalTime->mm % 10); // jednostki minut
        Global::fClockAngleDeg[3] = 36.0 * (GlobalTime->mm / 10); // dziesiątki minut
        Global::fClockAngleDeg[4] = 36.0 * (GlobalTime->hh % 10); // jednostki godzin
        Global::fClockAngleDeg[5] = 36.0 * (GlobalTime->hh / 10); // dziesiątki godzin
        if (Global::fMoveLight >= 0.0)
        { // testowo ruch światła
            // double a=Global::fTimeAngleDeg/180.0*M_PI-M_PI; //kąt godzinny w radianach
            double a = fmod(Global::fTimeAngleDeg, 360.0) / 180.0 * M_PI -
                       M_PI; // kąt godzinny w radianach
            //(a) jest traktowane jako czas miejscowy, nie uwzględniający stref czasowych ani czasu
            // letniego
            // aby wyznaczyć strefę czasową, trzeba uwzględnić południk miejscowy
            // aby uwzględnić czas letni, trzeba sprawdzić dzień roku
            double L = Global::fLatitudeDeg / 180.0 * M_PI; // szerokość geograficzna
            double H = asin(cos(L) * cos(Global::fSunDeclination) * cos(a) +
                            sin(L) * sin(Global::fSunDeclination)); // kąt ponad horyzontem
            // double A=asin(cos(d)*sin(M_PI-a)/cos(H));
            // Declination=((0.322003-22.971*cos(t)-0.357898*cos(2*t)-0.14398*cos(3*t)+3.94638*sin(t)+0.019334*sin(2*t)+0.05928*sin(3*t)))*Pi/180
            // Altitude=asin(sin(Declination)*sin(latitude)+cos(Declination)*cos(latitude)*cos((15*(time-12))*(Pi/180)));
            // Azimuth=(acos((cos(latitude)*sin(Declination)-cos(Declination)*sin(latitude)*cos((15*(time-12))*(Pi/180)))/cos(Altitude)));
            // double A=acos(cos(L)*sin(d)-cos(d)*sin(L)*cos(M_PI-a)/cos(H));
            // dAzimuth = atan2(-sin( dHourAngle ),tan( dDeclination )*dCos_Latitude -
            // dSin_Latitude*dCos_HourAngle );
            double A = atan2(sin(a), tan(Global::fSunDeclination) * cos(L) - sin(L) * cos(a));
            vector3 lp = vector3(sin(A), tan(H), cos(A));
            lp = Normalize(lp); // przeliczenie na wektor długości 1.0
            Global::lightPos[0] = (float)lp.x;
            Global::lightPos[1] = (float)lp.y;
            Global::lightPos[2] = (float)lp.z;
            glLightfv(GL_LIGHT0, GL_POSITION, Global::lightPos); // daylight position
            if (H > 0)
            { // słońce ponad horyzontem
                Global::ambientDayLight[0] = Global::ambientLight[0];
                Global::ambientDayLight[1] = Global::ambientLight[1];
                Global::ambientDayLight[2] = Global::ambientLight[2];
                if (H > 0.02) // ponad 1.146° zaczynają się cienie
                {
                    Global::diffuseDayLight[0] =
                        Global::diffuseLight[0]; // od wschodu do zachodu maksimum ???
                    Global::diffuseDayLight[1] = Global::diffuseLight[1];
                    Global::diffuseDayLight[2] = Global::diffuseLight[2];
                    Global::specularDayLight[0] = Global::specularLight[0]; // podobnie specular
                    Global::specularDayLight[1] = Global::specularLight[1];
                    Global::specularDayLight[2] = Global::specularLight[2];
                }
                else
                {
                    Global::diffuseDayLight[0] =
                        50 * H * Global::diffuseLight[0]; // wschód albo zachód
                    Global::diffuseDayLight[1] = 50 * H * Global::diffuseLight[1];
                    Global::diffuseDayLight[2] = 50 * H * Global::diffuseLight[2];
                    Global::specularDayLight[0] =
                        50 * H * Global::specularLight[0]; // podobnie specular
                    Global::specularDayLight[1] = 50 * H * Global::specularLight[1];
                    Global::specularDayLight[2] = 50 * H * Global::specularLight[2];
                }
            }
            else
            { // słońce pod horyzontem
                GLfloat lum = 3.1831 * (H > -0.314159 ? 0.314159 + H :
                                                        0.0); // po zachodzie ambient się ściemnia
                Global::ambientDayLight[0] = lum * Global::ambientLight[0];
                Global::ambientDayLight[1] = lum * Global::ambientLight[1];
                Global::ambientDayLight[2] = lum * Global::ambientLight[2];
                Global::diffuseDayLight[0] =
                    Global::noLight[0]; // od zachodu do wschodu nie ma diffuse
                Global::diffuseDayLight[1] = Global::noLight[1];
                Global::diffuseDayLight[2] = Global::noLight[2];
                Global::specularDayLight[0] = Global::noLight[0]; // ani specular
                Global::specularDayLight[1] = Global::noLight[1];
                Global::specularDayLight[2] = Global::noLight[2];
            }
            // Calculate sky colour according to time of day.
            // GLfloat sin_t = sin(PI * time_of_day / 12.0);
            // back_red = 0.3 * (1.0 - sin_t);
            // back_green = 0.9 * sin_t;
            // back_blue = sin_t + 0.4, 1.0;
            // aktualizacja świateł
            glLightfv(GL_LIGHT0, GL_AMBIENT, Global::ambientDayLight);
            glLightfv(GL_LIGHT0, GL_DIFFUSE, Global::diffuseDayLight);
            glLightfv(GL_LIGHT0, GL_SPECULAR, Global::specularDayLight);
        }
        Global::fLuminance = // to posłuży również do zapalania latarń
            +0.150 * (Global::diffuseDayLight[0] + Global::ambientDayLight[0]) // R
            + 0.295 * (Global::diffuseDayLight[1] + Global::ambientDayLight[1]) // G
            + 0.055 * (Global::diffuseDayLight[2] + Global::ambientDayLight[2]); // B
        if (Global::fMoveLight >= 0.0)
        { // przeliczenie koloru nieba
            vector3 sky = vector3(Global::AtmoColor[0], Global::AtmoColor[1], Global::AtmoColor[2]);
            if (Global::fLuminance < 0.25)
            { // przyspieszenie zachodu/wschodu
                sky *= 4.0 * Global::fLuminance; // nocny kolor nieba
                GLfloat fog[3];
                fog[0] = Global::FogColor[0] * 4.0 * Global::fLuminance;
                fog[1] = Global::FogColor[1] * 4.0 * Global::fLuminance;
                fog[2] = Global::FogColor[2] * 4.0 * Global::fLuminance;
                glFogfv(GL_FOG_COLOR, fog); // nocny kolor mgły
            }
            else
                glFogfv(GL_FOG_COLOR, Global::FogColor); // kolor mgły
            glClearColor(sky.x, sky.y, sky.z, 0.0); // kolor nieba
        }
    } // koniec działań niewykonywanych podczas pauzy
    // Console::Update(); //tu jest zależne od FPS, co nie jest korzystne
    if (Global::bActive)
    { // obsługa ruchu kamery tylko gdy okno jest aktywne
        if (Console::Pressed(VK_LBUTTON))
        {
            Camera.Reset(); // likwidacja obrotów - patrzy horyzontalnie na południe
            // if (!FreeFlyModeFlag) //jeśli wewnątrz - patrzymy do tyłu
            // Camera.LookAt=Train->pMechPosition-Normalize(Train->GetDirection())*10;
            if (Controlled ? LengthSquared3(Controlled->GetPosition() - Camera.Pos) < 2250000 :
                             false) // gdy bliżej niż 1.5km
                Camera.LookAt = Controlled->GetPosition();
            else
            {
                TDynamicObject *d =
                    Ground.DynamicNearest(Camera.Pos, 300); // szukaj w promieniu 300m
                if (!d)
                    d = Ground.DynamicNearest(Camera.Pos,
                                              1000); // dalej szukanie, jesli bliżej nie ma
                if (d && pDynamicNearest) // jeśli jakiś jest znaleziony wcześniej
                    if (100.0 * LengthSquared3(d->GetPosition() - Camera.Pos) >
                        LengthSquared3(pDynamicNearest->GetPosition() - Camera.Pos))
                        d = pDynamicNearest; // jeśli najbliższy nie jest 10 razy bliżej niż
                // poprzedni najbliższy, zostaje poprzedni
                if (d)
                    pDynamicNearest = d; // zmiana na nowy, jeśli coś znaleziony niepusty
                if (pDynamicNearest)
                    Camera.LookAt = pDynamicNearest->GetPosition();
            }
            if (FreeFlyModeFlag)
                Camera.RaLook(); // jednorazowe przestawienie kamery
        }
        else if (Console::Pressed(VK_RBUTTON)) //||Console::Pressed(VK_F4))
            FollowView(false); // bez wyciszania dźwięków
        else if (Global::iTextMode == -1)
        { // tu mozna dodac dopisywanie do logu przebiegu lokomotywy
            WriteLog("Number of textures used: " + std::to_string(Global::iTextures));
            return false;
        }
        Camera.Update(); // uwzględnienie ruchu wywołanego klawiszami
    } // koniec bloku pomijanego przy nieaktywnym oknie
    // poprzednie jakoś tam działało
    double dt = Timer::GetDeltaRenderTime(); // nie uwzględnia pauzowania ani mnożenia czasu
    fTime50Hz +=
        dt; // w pauzie też trzeba zliczać czas, bo przy dużym FPS będzie problem z odczytem ramek
    if (fTime50Hz >= 0.2)
        Console::Update(); // to i tak trzeba wywoływać
    dt = Timer::GetDeltaTime(); // 0.0 gdy pauza
    fTimeBuffer += dt; //[s] dodanie czasu od poprzedniej ramki
    if (fTimeBuffer >= fMaxDt) // jest co najmniej jeden krok; normalnie 0.01s
    { // Ra: czas dla fizyki jest skwantowany - fizykę lepiej przeliczać stałym krokiem
        // tak można np. moc silników itp., ale ruch musi być przeliczany w każdej klatce, bo
        // inaczej skacze
        Global::tranTexts.Update(); // obiekt obsługujący stenogramy dźwięków na ekranie
        Console::Update(); // obsługa cykli PoKeys (np. aktualizacja wyjść analogowych)
        double iter =
            ceil(fTimeBuffer / fMaxDt); // ile kroków się zmieściło od ostatniego sprawdzania?
        int n = int(iter); // ile kroków jako int
        fTimeBuffer -= iter * fMaxDt; // reszta czasu na potem (do bufora)
        if (n > 20)
            n = 20; // Ra: jeżeli FPS jest zatrważająco niski, to fizyka nie może zająć całkowicie
// procesora
#if 0
  Ground.UpdatePhys(fMaxDt,n); //Ra: teraz czas kroku jest (względnie) stały
  if (DebugModeFlag)
   if (Global::bActive) //nie przyspieszać, gdy jedzie w tle :)
    if (GetAsyncKeyState(VK_ESCAPE)<0)
    {//yB dodał przyspieszacz fizyki
     Ground.UpdatePhys(fMaxDt,n);
     Ground.UpdatePhys(fMaxDt,n);
     Ground.UpdatePhys(fMaxDt,n);
     Ground.UpdatePhys(fMaxDt,n); //w sumie 5 razy
    }
#endif
    }
    // awaria PoKeys mogła włączyć pauzę - przekazać informację
    if (Global::iMultiplayer) // dajemy znać do serwera o wykonaniu
        if (iPause != Global::iPause)
        { // przesłanie informacji o pauzie do programu nadzorującego
            Ground.WyslijParam(5, 3); // ramka 5 z czasem i stanem zapauzowania
            iPause = Global::iPause;
        }
    double iter;
    int n = 1;
    if (dt > fMaxDt) // normalnie 0.01s
    {
        iter = ceil(dt / fMaxDt);
        n = iter;
        dt = dt / iter; // Ra: fizykę lepiej by było przeliczać ze stałym krokiem
        if (n > 20)
            n = 20; // McZapkie-081103: przesuniecie granicy FPS z 10 na 5
    }
    // else n=1;
    // blablabla
    // Ground.UpdatePhys(dt,n); //na razie tu //2014-12: yB przeniósł do Ground.Update() :(
    Ground.Update(dt, n); // tu zrobić tylko coklatkową aktualizację przesunięć
    if (DebugModeFlag)
        if (Global::bActive) // nie przyspieszać, gdy jedzie w tle :)
            if (GetAsyncKeyState(VK_ESCAPE) < 0)
            { // yB dodał przyspieszacz fizyki
                Ground.Update(dt, n);
                Ground.Update(dt, n);
                Ground.Update(dt, n);
                Ground.Update(dt, n); // 5 razy
            }
    dt = Timer::GetDeltaTime(); // czas niekwantowany
    if (Camera.Type == tp_Follow)
    {
        if (Train)
        { // jeśli jazda w kabinie, przeliczyć trzeba parametry kamery
            Train->UpdateMechPosition(dt /
                                      Global::fTimeSpeed); // ograniczyć telepanie po przyspieszeniu
            vector3 tempangle;
            double modelrotate;
            tempangle =
                Controlled->VectorFront() * (Controlled->MoverParameters->ActiveCab == -1 ? -1 : 1);
            modelrotate = atan2(-tempangle.x, tempangle.z);
            if (Console::Pressed(VK_CONTROL) ? (Console::Pressed(Global::Keys[k_MechLeft]) ||
                                                Console::Pressed(Global::Keys[k_MechRight])) :
                                               false)
            { // jeśli lusterko lewe albo prawe (bez rzucania na razie)
                bool lr = Console::Pressed(Global::Keys[k_MechLeft]);
#if 0
    Camera.Pos=Train->MirrorPosition(lr); //robocza wartość
    if (Controlled->MoverParameters->ActiveCab<0) lr=!lr; //w drugiej kabinie odwrotnie jest środek
    Camera.LookAt=Controlled->GetPosition()+vector3(lr?2.0:-2.0,Camera.Pos.y,0); //trochę na zewnątrz, użyć szerokości pojazdu
    //Camera.LookAt=Train->pMechPosition+Train->GetDirection()*Train->Dynamic()->MoverParameters->ActiveCab;
    Camera.Pos+=Controlled->GetPosition();
    //Camera.RaLook(); //jednorazowe przestawienie kamery
    Camera.Yaw=0; //odchylenie na bok od Camera.LookAt
#else
                // Camera.Yaw powinno być wyzerowane, aby po powrocie patrzeć do przodu
                Camera.Pos =
                    Controlled->GetPosition() + Train->MirrorPosition(lr); // pozycja lusterka
                Camera.Yaw = 0; // odchylenie na bok od Camera.LookAt
                if (Train->Dynamic()->MoverParameters->ActiveCab == 0)
                    Camera.LookAt = Camera.Pos - Train->GetDirection(); // gdy w korytarzu
                else if (Console::Pressed(VK_SHIFT))
                { // patrzenie w bok przez szybę
                    Camera.LookAt = Camera.Pos -
                                    (lr ? -1 : 1) * Train->Dynamic()->VectorLeft() *
                                        Train->Dynamic()->MoverParameters->ActiveCab;
                    Global::SetCameraRotation(-modelrotate);
                }
                else
                { // patrzenie w kierunku osi pojazdu, z uwzględnieniem kabiny - jakby z lusterka,
                    // ale bez odbicia
                    Camera.LookAt = Camera.Pos -
                                    Train->GetDirection() *
                                        Train->Dynamic()->MoverParameters->ActiveCab; //-1 albo 1
                    Global::SetCameraRotation(M_PI -
                                              modelrotate); // tu już trzeba uwzględnić lusterka
                }
#endif
                Camera.Roll =
                    atan(Train->pMechShake.x * Train->fMechRoll); // hustanie kamery na boki
                Camera.Pitch =
                    atan(Train->vMechVelocity.z * Train->fMechPitch); // hustanie kamery przod tyl
                Camera.vUp = Controlled->VectorUp();
            }
            else
            { // patrzenie standardowe
                Camera.Pos = Train->pMechPosition; // Train.GetPosition1();
                if (!Global::iPause)
                { // podczas pauzy nie przeliczać kątów przypadkowymi wartościami
                    Camera.Roll =
                        atan(Train->pMechShake.x * Train->fMechRoll); // hustanie kamery na boki
                    Camera.Pitch -= atan(Train->vMechVelocity.z *
                                         Train->fMechPitch); // hustanie kamery przod tyl //Ra: tu
                    // jest uciekanie kamery w górę!!!
                }
                // ABu011104: rzucanie pudlem
                vector3 temp;
                if (abs(Train->pMechShake.y) < 0.25)
                    temp = vector3(0, 0, 6 * Train->pMechShake.y);
                else if ((Train->pMechShake.y) > 0)
                    temp = vector3(0, 0, 6 * 0.25);
                else
                    temp = vector3(0, 0, -6 * 0.25);
                if (Controlled)
                    Controlled->ABuSetModelShake(temp);
                // ABu: koniec rzucania

                if (Train->Dynamic()->MoverParameters->ActiveCab == 0)
                    Camera.LookAt = Train->pMechPosition + Train->GetDirection(); // gdy w korytarzu
                else // patrzenie w kierunku osi pojazdu, z uwzględnieniem kabiny
                    Camera.LookAt = Train->pMechPosition +
                                    Train->GetDirection() *
                                        Train->Dynamic()->MoverParameters->ActiveCab; //-1 albo 1
                Camera.vUp = Train->GetUp();
                Global::SetCameraRotation(Camera.Yaw -
                                          modelrotate); // tu już trzeba uwzględnić lusterka
            }
        }
    }
    else
    { // kamera nieruchoma
        Global::SetCameraRotation(Camera.Yaw - M_PI);
    }
    Ground.CheckQuery();
    // przy 0.25 smuga gaśnie o 6:37 w Quarku, a mogłaby już 5:40
    // Ra 2014-12: przy 0.15 się skarżyli, że nie widać smug => zmieniłem na 0.25
    if (Train) // jeśli nie usunięty
        Global::bSmudge =
            FreeFlyModeFlag ? false : ((Train->Dynamic()->fShade <= 0.0) ?
                                           (Global::fLuminance <= 0.25) :
                                           (Train->Dynamic()->fShade * Global::fLuminance <= 0.25));

    if (!Render())
        return false;

    //**********************************************************************************************************

    if (Train)
    { // rendering kabiny gdy jest oddzielnym modelem i ma byc wyswietlana
        glPushMatrix();
        // ABu: Rendering kabiny jako ostatniej, zeby bylo widac przez szyby, tylko w widoku ze
        // srodka
        if ((Train->Dynamic()->mdKabina != Train->Dynamic()->mdModel) &&
            Train->Dynamic()->bDisplayCab && !FreeFlyModeFlag)
        {
            vector3 pos = Train->Dynamic()->GetPosition(); // wszpółrzędne pojazdu z kabiną
            // glTranslatef(pos.x,pos.y,pos.z); //przesunięcie o wektor (tak było i trzęsło)
            // aby pozbyć się choć trochę trzęsienia, trzeba by nie przeliczać kabiny do punktu
            // zerowego scenerii
            glLoadIdentity(); // zacząć od macierzy jedynkowej
            Camera.SetCabMatrix(pos); // widok z kamery po przesunięciu
            glMultMatrixd(Train->Dynamic()->mMatrix.getArray()); // ta macierz nie ma przesunięcia

            //*yB: moje smuuugi 1
            if (Global::bSmudge)
            { // Ra: uwzględniłem zacienienie pojazdu przy zapalaniu smug
                // 1. warunek na smugę wyznaczyc wcześniej
                // 2. jeśli smuga włączona, nie renderować pojazdu użytkownika w DynObj
                // 3. jeśli smuga właczona, wyrenderować pojazd użytkownia po dodaniu smugi do sceny
                if (Train->Controlled()->Battery)
                { // trochę na skróty z tą baterią
					if (Global::bOldSmudge == true)
					{
					glBlendFunc(GL_ONE_MINUS_SRC_ALPHA, GL_ONE);
                    //    glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_DST_COLOR);
                    //    glBlendFunc(GL_SRC_ALPHA_SATURATE,GL_ONE);
                    glDisable(GL_DEPTH_TEST);
                    glDisable(GL_LIGHTING);
                    glDisable(GL_FOG);
                    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
                    glBindTexture(GL_TEXTURE_2D, light); // Select our texture
                    glBegin(GL_QUADS);
                    float fSmudge =
                        Train->Dynamic()->MoverParameters->DimHalf.y + 7; // gdzie zaczynać smugę
                    if (Train->Controlled()->iLights[0] & 21)
                    { // wystarczy jeden zapalony z przodu
                        glTexCoord2f(0, 0);
                        glVertex3f(15.0, 0.0, +fSmudge); // rysowanie względem położenia modelu
                        glTexCoord2f(1, 0);
                        glVertex3f(-15.0, 0.0, +fSmudge);
                        glTexCoord2f(1, 1);
                        glVertex3f(-15.0, 2.5, 250.0);
                        glTexCoord2f(0, 1);
                        glVertex3f(15.0, 2.5, 250.0);
                    }
                    if (Train->Controlled()->iLights[1] & 21)
                    { // wystarczy jeden zapalony z tyłu
                        glTexCoord2f(0, 0);
                        glVertex3f(-15.0, 0.0, -fSmudge);
                        glTexCoord2f(1, 0);
                        glVertex3f(15.0, 0.0, -fSmudge);
                        glTexCoord2f(1, 1);
                        glVertex3f(15.0, 2.5, -250.0);
                        glTexCoord2f(0, 1);
                        glVertex3f(-15.0, 2.5, -250.0);
                    }
                    glEnd();

                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                    glEnable(GL_DEPTH_TEST);
                    // glEnable(GL_LIGHTING); //i tak się włączy potem
                    glEnable(GL_FOG);
					}
					else
					{
						glBlendFunc(GL_DST_COLOR, GL_ONE);
						glDepthFunc(GL_GEQUAL);
						glAlphaFunc(GL_GREATER, 0.004);
						// glDisable(GL_DEPTH_TEST);
						glDisable(GL_LIGHTING);
						glDisable(GL_FOG);
						//glColor4f(0.15f, 0.15f, 0.15f, 0.25f);
						glBindTexture(GL_TEXTURE_2D, light); // Select our texture
						//float ddl = (0.15*Global::diffuseDayLight[0]+0.295*Global::diffuseDayLight[1]+0.055*Global::diffuseDayLight[2]); //0.24:0
						glBegin(GL_QUADS);
						float fSmudge = Train->Dynamic()->MoverParameters->DimHalf.y + 7; // gdzie zaczynać smugę
						if (Train->Controlled()->iLights[0] & 21)
						{ // wystarczy jeden zapalony z przodu
							for (int i = 15; i <= 35; i++)
							{
								float z = i * i * i * 0.01f;//25/4;
								//float C = (36 - i*0.5)*0.005*(1.5 - sqrt(ddl));
								float C = (36 - i*0.5)*0.005*sqrt((1/sqrt(Global::fLuminance+0.015))-1);
								glColor4f(C, C, C, 0.25f);
								glTexCoord2f(0, 0);  glVertex3f(-10 / 2 - 2 * i / 4, 6.0 + 0.3*z, 13 + 1.7*z / 3);
								glTexCoord2f(1, 0);  glVertex3f(10 / 2 + 2 * i / 4, 6.0 + 0.3*z, 13 + 1.7*z / 3);
								glTexCoord2f(1, 1);  glVertex3f(10 / 2 + 2 * i / 4, -5.0 - 0.5*z, 13 + 1.7*z / 3);
								glTexCoord2f(0, 1);  glVertex3f(-10 / 2 - 2 * i / 4, -5.0 - 0.5*z, 13 + 1.7*z / 3);
							}
						}
						if (Train->Controlled()->iLights[1] & 21)
						{ // wystarczy jeden zapalony z tyłu
							for (int i = 15; i <= 35; i++)
							{
								float z = i * i * i * 0.01f;//25/4;
								float C = (36 - i*0.5)*0.005*sqrt((1/sqrt(Global::fLuminance+0.015))-1);
								glColor4f(C, C, C, 0.25f);
								glTexCoord2f(0, 0);  glVertex3f(10 / 2 + 2 * i / 4, 6.0 + 0.3*z, -13 - 1.7*z / 3);
								glTexCoord2f(1, 0);  glVertex3f(-10 / 2 - 2 * i / 4, 6.0 + 0.3*z, -13 - 1.7*z / 3);
								glTexCoord2f(1, 1);  glVertex3f(-10 / 2 - 2 * i / 4, -5.0 - 0.5*z, -13 - 1.7*z / 3);
								glTexCoord2f(0, 1);  glVertex3f(10 / 2 + 2 * i / 4, -5.0 - 0.5*z, -13 - 1.7*z / 3);
							}
						}
						glEnd();

						glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
						// glEnable(GL_DEPTH_TEST);
						glAlphaFunc(GL_GREATER, 0.04);
						glDepthFunc(GL_LEQUAL);
						glEnable(GL_LIGHTING); //i tak się włączy potem
						glEnable(GL_FOG);
					}
                }
                glEnable(GL_LIGHTING); // po renderowaniu smugi jest to wyłączone
                // Ra: pojazd użytkownika należało by renderować po smudze, aby go nie rozświetlała
                Global::bSmudge = false; // aby model użytkownika się teraz wyrenderował
                Train->Dynamic()->Render();
                Train->Dynamic()->RenderAlpha(); // przezroczyste fragmenty pojazdów na torach
            } // yB: moje smuuugi 1 - koniec*/
            else
                glEnable(GL_LIGHTING); // po renderowaniu drutów może być to wyłączone

            if (Train->Dynamic()->mdKabina) // bo mogła zniknąć przy przechodzeniu do innego pojazdu
            { // oswietlenie kabiny
                GLfloat ambientCabLight[4] = {0.5f, 0.5f, 0.5f, 1.0f};
                GLfloat diffuseCabLight[4] = {0.5f, 0.5f, 0.5f, 1.0f};
                GLfloat specularCabLight[4] = {0.5f, 0.5f, 0.5f, 1.0f};
                for (int li = 0; li < 3; li++)
                { // przyciemnienie standardowe
                    ambientCabLight[li] = Global::ambientDayLight[li] * 0.9;
                    diffuseCabLight[li] = Global::diffuseDayLight[li] * 0.5;
                    specularCabLight[li] = Global::specularDayLight[li] * 0.5;
                }
                switch (Train->Dynamic()->MyTrack->eEnvironment)
                { // wpływ świetła zewnętrznego
                case e_canyon:
                {
                    for (int li = 0; li < 3; li++)
                    {
                        diffuseCabLight[li] *= 0.6;
                        specularCabLight[li] *= 0.7;
                    }
                }
                break;
                case e_tunnel:
                {
                    for (int li = 0; li < 3; li++)
                    {
                        ambientCabLight[li] *= 0.3;
                        diffuseCabLight[li] *= 0.1;
                        specularCabLight[li] *= 0.2;
                    }
                }
                break;
                }
                switch (Train->iCabLightFlag) // Ra: uzeleżnic od napięcia w obwodzie sterowania
                { // hunter-091012: uzaleznienie jasnosci od przetwornicy
                case 0: //światło wewnętrzne zgaszone
                    break;
                case 1: //światło wewnętrzne przygaszone (255 216 176)
                    if (Train->Dynamic()->MoverParameters->ConverterFlag ==
                        true) // jasnosc dla zalaczonej przetwornicy
                    {
                        ambientCabLight[0] = Max0R(0.700, ambientCabLight[0]) * 0.75; // R
                        ambientCabLight[1] = Max0R(0.593, ambientCabLight[1]) * 0.75; // G
                        ambientCabLight[2] = Max0R(0.483, ambientCabLight[2]) * 0.75; // B

                        for (int i = 0; i < 3; i++)
                            if (ambientCabLight[i] <= (Global::ambientDayLight[i] * 0.9))
                                ambientCabLight[i] = Global::ambientDayLight[i] * 0.9;
                    }
                    else
                    {
                        ambientCabLight[0] = Max0R(0.700, ambientCabLight[0]) * 0.375; // R
                        ambientCabLight[1] = Max0R(0.593, ambientCabLight[1]) * 0.375; // G
                        ambientCabLight[2] = Max0R(0.483, ambientCabLight[2]) * 0.375; // B

                        for (int i = 0; i < 3; i++)
                            if (ambientCabLight[i] <= (Global::ambientDayLight[i] * 0.9))
                                ambientCabLight[i] = Global::ambientDayLight[i] * 0.9;
                    }
                    break;
                case 2: //światło wewnętrzne zapalone (255 216 176)
                    if (Train->Dynamic()->MoverParameters->ConverterFlag ==
                        true) // jasnosc dla zalaczonej przetwornicy
                    {
                        ambientCabLight[0] = Max0R(1.000, ambientCabLight[0]); // R
                        ambientCabLight[1] = Max0R(0.847, ambientCabLight[1]); // G
                        ambientCabLight[2] = Max0R(0.690, ambientCabLight[2]); // B

                        for (int i = 0; i < 3; i++)
                            if (ambientCabLight[i] <= (Global::ambientDayLight[i] * 0.9))
                                ambientCabLight[i] = Global::ambientDayLight[i] * 0.9;
                    }
                    else
                    {
                        ambientCabLight[0] = Max0R(1.000, ambientCabLight[0]) * 0.5; // R
                        ambientCabLight[1] = Max0R(0.847, ambientCabLight[1]) * 0.5; // G
                        ambientCabLight[2] = Max0R(0.690, ambientCabLight[2]) * 0.5; // B

                        for (int i = 0; i < 3; i++)
                            if (ambientCabLight[i] <= (Global::ambientDayLight[i] * 0.9))
                                ambientCabLight[i] = Global::ambientDayLight[i] * 0.9;
                    }
                    break;
                }
                glLightfv(GL_LIGHT0, GL_AMBIENT, ambientCabLight);
                glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuseCabLight);
                glLightfv(GL_LIGHT0, GL_SPECULAR, specularCabLight);
                if (Global::bUseVBO)
                { // renderowanie z użyciem VBO
                    Train->Dynamic()->mdKabina->RaRender(0.0, Train->Dynamic()->ReplacableSkinID,
                                                         Train->Dynamic()->iAlpha);
                    Train->Dynamic()->mdKabina->RaRenderAlpha(
                        0.0, Train->Dynamic()->ReplacableSkinID, Train->Dynamic()->iAlpha);
                }
                else
                { // renderowanie z Display List
                    Train->Dynamic()->mdKabina->Render(0.0, Train->Dynamic()->ReplacableSkinID,
                                                       Train->Dynamic()->iAlpha);
                    Train->Dynamic()->mdKabina->RenderAlpha(0.0, Train->Dynamic()->ReplacableSkinID,
                                                            Train->Dynamic()->iAlpha);
                }
                // przywrócenie standardowych, bo zawsze są zmieniane
                glLightfv(GL_LIGHT0, GL_AMBIENT, Global::ambientDayLight);
                glLightfv(GL_LIGHT0, GL_DIFFUSE, Global::diffuseDayLight);
                glLightfv(GL_LIGHT0, GL_SPECULAR, Global::specularDayLight);
            }
        } // koniec: if (Train->Dynamic()->mdKabina)
        glPopMatrix();
        //**********************************************************************************************************
    } // koniec: if (Train)
    if (DebugModeFlag && !Global::iTextMode)
    {
        OutText1 = "  FPS: ";
        OutText1 += to_string(Timer::GetFPS(), 2);
        OutText1 += Global::iSlowMotion ? "s" : "n";

        OutText1 += (Timer::GetDeltaTime() >= 0.2) ? "!" : " ";
        // if (GetDeltaTime()>=0.2) //Ra: to za bardzo miota tekstem!
        // {
        //     OutText1+= " Slowing Down !!! ";
        // }
    }
    /*if (Console::Pressed(VK_F5))
       {Global::slowmotion=true;};
    if (Console::Pressed(VK_F6))
       {Global::slowmotion=false;};*/

    if (Global::iTextMode == VK_F8)
    {
        Global::iViewMode = VK_F8;
        OutText1 = "  FPS: ";
        OutText1 += to_string(Timer::GetFPS(), 2);
		//OutText1 += sprintf();
        if (Global::iSlowMotion)
            OutText1 += " (slowmotion " + to_string(Global::iSlowMotion) + ")";
        OutText1 += ", sectors: ";
        OutText1 += to_string(Ground.iRendered);
    }

    // if (Console::Pressed(VK_F7))
    //{
    //  OutText1=FloatToStrF(Controlled->MoverParameters->Couplers[0].CouplingFlag,ffFixed,2,0)+",
    //  ";
    //  OutText1+=FloatToStrF(Controlled->MoverParameters->Couplers[1].CouplingFlag,ffFixed,2,0);
    //}

    /*
    if (Console::Pressed(VK_F5))
       {
          int line=2;
          OutText1="Time: "+FloatToStrF(GlobalTime->hh,ffFixed,2,0)+":"
                  +FloatToStrF(GlobalTime->mm,ffFixed,2,0)+", ";
          OutText1+="distance: ";
          OutText1+="34.94";
          OutText2="Next station: ";
          OutText2+=FloatToStrF(Controlled->TrainParams->TimeTable[line].km,ffFixed,2,2)+" km, ";
          OutText2+=AnsiString(Controlled->TrainParams->TimeTable[line].StationName)+", ";
          OutText2+=AnsiString(Controlled->TrainParams->TimeTable[line].StationWare);
          OutText3="Arrival: ";
          if(Controlled->TrainParams->TimeTable[line].Ah==-1)
          {
             OutText3+="--:--";
          }
          else
          {
             OutText3+=FloatToStrF(Controlled->TrainParams->TimeTable[line].Ah,ffFixed,2,0)+":";
             OutText3+=FloatToStrF(Controlled->TrainParams->TimeTable[line].Am,ffFixed,2,0)+" ";
          }
          OutText3+=" Departure: ";
          OutText3+=FloatToStrF(Controlled->TrainParams->TimeTable[line].Dh,ffFixed,2,0)+":";
          OutText3+=FloatToStrF(Controlled->TrainParams->TimeTable[line].Dm,ffFixed,2,0)+" ";
       };
//    */
    /*
    if (Console::Pressed(VK_F6))
    {
       //GlobalTime->UpdateMTableTime(100);
       //OutText1=FloatToStrF(SquareMagnitude(Global::pCameraPosition-Controlled->GetPosition()),ffFixed,10,0);
       //OutText1=FloatToStrF(Global::TnijSzczegoly,ffFixed,7,0)+", ";
       //OutText1+=FloatToStrF(dta,ffFixed,2,4)+", ";
       OutText1+= FloatToStrF(GetFPS(),ffFixed,6,2);
       OutText1+= FloatToStrF(Global::ABuDebug,ffFixed,6,15);
    };
    */
    if (Global::changeDynObj)
    { // ABu zmiana pojazdu - przejście do innego
        // Ra: to nie może być tak robione, to zbytnia proteza jest
        Train->Silence(); // wyłączenie dźwięków opuszczanej kabiny
        if (Train->Dynamic()->Mechanik) // AI może sobie samo pójść
            if (!Train->Dynamic()->Mechanik->AIControllFlag) // tylko jeśli ręcznie prowadzony
            { // jeśli prowadzi AI, to mu nie robimy dywersji!
                Train->Dynamic()->MoverParameters->CabDeactivisation();
                Train->Dynamic()->Controller = AIdriver;
                // Train->Dynamic()->MoverParameters->SecuritySystem.Status=0; //rozwala CA w EZT
                Train->Dynamic()->MoverParameters->ActiveCab = 0;
                Train->Dynamic()->MoverParameters->BrakeLevelSet(
					Train->Dynamic()->MoverParameters->Handle->GetPos(
						bh_NP)); //rozwala sterowanie hamulcem GF 04-2016
                Train->Dynamic()->MechInside = false;
            }
        // int CabNr;
        TDynamicObject *temp = Global::changeDynObj;
        // CabNr=temp->MoverParameters->ActiveCab;
        /*
             if (Train->Dynamic()->MoverParameters->ActiveCab==-1)
             {
              temp=Train->Dynamic()->NextConnected; //pojazd od strony sprzęgu 1
              CabNr=(Train->Dynamic()->NextConnectedNo==0)?1:-1;
             }
             else
             {
              temp=Train->Dynamic()->PrevConnected; //pojazd od strony sprzęgu 0
              CabNr=(Train->Dynamic()->PrevConnectedNo==0)?1:-1;
             }
        */
        Train->Dynamic()->bDisplayCab = false;
        Train->Dynamic()->ABuSetModelShake(vector3(0, 0, 0));
        /// Train->Dynamic()->MoverParameters->LimPipePress=-1;
        /// Train->Dynamic()->MoverParameters->ActFlowSpeed=0;
        /// Train->Dynamic()->Mechanik->CloseLog();
        /// SafeDelete(Train->Dynamic()->Mechanik);

        // Train->Dynamic()->mdKabina=NULL;
        if (Train->Dynamic()->Mechanik) // AI może sobie samo pójść
            if (!Train->Dynamic()->Mechanik->AIControllFlag) // tylko jeśli ręcznie prowadzony
                Train->Dynamic()->Mechanik->MoveTo(temp); // przsunięcie obiektu zarządzającego
        // Train->DynamicObject=NULL;
        Train->DynamicSet(temp);
        Controlled = temp;
        mvControlled = Controlled->ControlledFind()->MoverParameters;
        Global::asHumanCtrlVehicle = Train->Dynamic()->GetName();
        if (Train->Dynamic()->Mechanik) // AI może sobie samo pójść
            if (!Train->Dynamic()->Mechanik->AIControllFlag) // tylko jeśli ręcznie prowadzony
            {
                Train->Dynamic()->MoverParameters->LimPipePress =
                    Controlled->MoverParameters->PipePress;
                // Train->Dynamic()->MoverParameters->ActFlowSpeed=0;
                // Train->Dynamic()->MoverParameters->SecuritySystem.Status=1;
                // Train->Dynamic()->MoverParameters->ActiveCab=CabNr;
                Train->Dynamic()
                    ->MoverParameters->CabActivisation(); // załączenie rozrządu (wirtualne kabiny)
                Train->Dynamic()->Controller = Humandriver;
                Train->Dynamic()->MechInside = true;
                // Train->Dynamic()->Mechanik=new
                // TController(l,r,Controlled->Controller,&Controlled->MoverParameters,&Controlled->TrainParams,Aggressive);
                // Train->InitializeCab(CabNr,Train->Dynamic()->asBaseDir+Train->Dynamic()->MoverParameters->TypeName+".mmd");
            }
        Train->InitializeCab(Train->Dynamic()->MoverParameters->CabNo,
                             Train->Dynamic()->asBaseDir +
                                 Train->Dynamic()->MoverParameters->TypeName + ".mmd");
        if (!FreeFlyModeFlag)
        {
            Global::pUserDynamic = Controlled; // renerowanie względem kamery
            Train->Dynamic()->bDisplayCab = true;
            Train->Dynamic()->ABuSetModelShake(
                vector3(0, 0, 0)); // zerowanie przesunięcia przed powrotem?
            Train->MechStop();
            FollowView(); // na pozycję mecha
        }
        Global::changeDynObj = NULL;
    }

    glDisable(GL_LIGHTING);
    if (Controlled)
        SetWindowText(hWnd, Controlled->MoverParameters->Name.c_str());
    else
        SetWindowText(hWnd, Global::SceneryFile.c_str()); // nazwa scenerii
    glBindTexture(GL_TEXTURE_2D, 0);
    glColor4f(1.0f, 0.0f, 0.0f, 1.0f);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -0.50f);

    if (Global::iTextMode == VK_F1)
    { // tekst pokazywany po wciśnięciu [F1]
        // Global::iViewMode=VK_F1;
        glColor3f(1.0f, 1.0f, 1.0f); // a, damy białym
        OutText1 = "Time: " + to_string((int)GlobalTime->hh) + ":";
        int i = GlobalTime->mm; // bo inaczej potrafi zrobić "hh:010"
        if (i < 10)
            OutText1 += "0";
        OutText1 += to_string(i); // minuty
        OutText1 += ":";
        i = floor(GlobalTime->mr); // bo inaczej potrafi zrobić "hh:mm:010"
        if (i < 10)
            OutText1 += "0";
        OutText1 += to_string(i);
        if (Global::iPause)
            OutText1 += " - paused";
        if (Controlled)
            if (Controlled->Mechanik)
            {
                OutText2 = Controlled->Mechanik->Relation();
                if (!OutText2.empty()) // jeśli jest podana relacja, to dodajemy punkt następnego
                    // zatrzymania
                    OutText2 =
                        Global::Bezogonkow(OutText2 + ": -> " + Controlled->Mechanik->NextStop(),
                                           true); // dopisanie punktu zatrzymania
            }
        // double CtrlPos=mvControlled->MainCtrlPos;
        // double CtrlPosNo=mvControlled->MainCtrlPosNo;
        // OutText2="defrot="+FloatToStrF(1+0.4*(CtrlPos/CtrlPosNo),ffFixed,2,5);
        OutText3 = ""; // Pomoc w sterowaniu - [F9]";
        // OutText3=AnsiString(Global::pCameraRotationDeg); //kąt kamery względem północy
    }
    else if (Global::iTextMode == VK_F12)
    { // opcje włączenia i wyłączenia logowania
        OutText1 = "[0] Debugmode " + std::string(DebugModeFlag ? "(on)" : "(off)");
        OutText2 = "[1] log.txt " + std::string((Global::iWriteLogEnabled & 1) ? "(on)" : "(off)");
        OutText3 = "[2] Console " + std::string((Global::iWriteLogEnabled & 2) ? "(on)" : "(off)");
    }
    else if (Global::iTextMode == VK_F2)
    { // ABu: info dla najblizszego pojazdu!
        TDynamicObject *tmp = FreeFlyModeFlag ? Ground.DynamicNearest(Camera.Pos) :
                                                Controlled; // w trybie latania lokalizujemy wg mapy
        if (tmp)
        {
            if (Global::iScreenMode[Global::iTextMode - VK_F1] == 0)
            { // jeśli domyślny ekran po pierwszym naciśnięciu
                OutText3 = "";
                OutText1 = "Vehicle name: " + tmp->MoverParameters->Name;
                // yB       OutText1+="; d:  "+FloatToStrF(tmp->ABuGetDirection(),ffFixed,2,0);
                // OutText1=FloatToStrF(tmp->MoverParameters->Couplers[0].CouplingFlag,ffFixed,3,2)+",
                // ";
                // OutText1+=FloatToStrF(tmp->MoverParameters->Couplers[1].CouplingFlag,ffFixed,3,2);
                if (tmp->Mechanik) // jeśli jest prowadzący
                { // ostatnia komenda dla AI
                    OutText1 += ", command: " + tmp->Mechanik->OrderCurrent();
                }
                else if (tmp->ctOwner)
                    OutText1 += ", owned by " + tmp->ctOwner->OwnerName();
                if (!tmp->MoverParameters->CommandLast.empty())
                    OutText1 += ", put: " + tmp->MoverParameters->CommandLast;
                // OutText1+="; Cab="+AnsiString(tmp->MoverParameters->CabNo);
                OutText2 = "Damage status: " +
                           tmp->MoverParameters->EngineDescription(0); //+" Engine status: ";
                OutText2 += "; Brake delay: ";
                if ((tmp->MoverParameters->BrakeDelayFlag & bdelay_G) == bdelay_G)
                    OutText2 += "G";
                if ((tmp->MoverParameters->BrakeDelayFlag & bdelay_P) == bdelay_P)
                    OutText2 += "P";
                if ((tmp->MoverParameters->BrakeDelayFlag & bdelay_R) == bdelay_R)
                    OutText2 += "R";
                if ((tmp->MoverParameters->BrakeDelayFlag & bdelay_M) == bdelay_M)
                    OutText2 += "+Mg";
                OutText2 += ", BTP:" +
                            to_string(tmp->MoverParameters->LoadFlag, 0);
                // if ((tmp->MoverParameters->EnginePowerSource.SourceType==CurrentCollector) ||
                // (tmp->MoverParameters->TrainType==dt_EZT))
                {
                    OutText2 += "; pant. " +
                                to_string(tmp->MoverParameters->PantPress, 2);
                    OutText2 += (tmp->MoverParameters->bPantKurek3 ? "<ZG" : "|ZG");
                }

                //          OutText2+=AnsiString(",
                //          u:")+FloatToStrF(tmp->MoverParameters->u,ffFixed,3,3);
                //          OutText2+=AnsiString(",
                //          N:")+FloatToStrF(tmp->MoverParameters->Ntotal,ffFixed,4,0);
				OutText2 += ", MED:" +
					to_string(tmp->MoverParameters->LocalBrakePosA, 2);
				OutText2 += "+" + 
					to_string(tmp->MoverParameters->AnPos, 2);
				OutText2 += ", Ft:" +
					to_string(tmp->MoverParameters->Ft * 0.001f, 0);
				OutText2 += ", HV0:" +
					to_string(tmp->MoverParameters->HVCouplers[0][1], 0);
				OutText2 += "@" +
					to_string(tmp->MoverParameters->HVCouplers[0][0], 0);
				OutText2 += "+HV1:" +
					to_string(tmp->MoverParameters->HVCouplers[1][1], 0);
				OutText2 += "@" +
					to_string(tmp->MoverParameters->HVCouplers[1][0], 0);
				OutText2 += " TC:" +
					to_string(tmp->MoverParameters->TotalCurrent, 0);
				//          OutText3= AnsiString("BP:
                //          ")+FloatToStrF(tmp->MoverParameters->BrakePress,ffFixed,5,2)+AnsiString(",
                //          ");
                //          OutText3+= AnsiString("PP:
                //          ")+FloatToStrF(tmp->MoverParameters->PipePress,ffFixed,5,2)+AnsiString(",
                //          ");
                //          OutText3+= AnsiString("BVP:
                //          ")+FloatToStrF(tmp->MoverParameters->Volume,ffFixed,5,3)+AnsiString(",
                //          ");
                //          OutText3+=
                //          FloatToStrF(tmp->MoverParameters->CntrlPipePress,ffFixed,5,3)+AnsiString(",
                //          ");
                //          OutText3+=
                //          FloatToStrF(tmp->MoverParameters->Hamulec->GetCRP(),ffFixed,5,3)+AnsiString(",
                //          ");
                //          OutText3+=
                //          FloatToStrF(tmp->MoverParameters->BrakeStatus,ffFixed,5,0)+AnsiString(",
                //          ");
                //          OutText3+= AnsiString("HP:
                //          ")+FloatToStrF(tmp->MoverParameters->ScndPipePress,ffFixed,5,2)+AnsiString(".
                //          ");
                //      OutText2+=
                //      FloatToStrF(tmp->MoverParameters->CompressorPower,ffFixed,5,0)+AnsiString(",
                //      ");
                // yB      if(tmp->MoverParameters->BrakeSubsystem==Knorr) OutText2+=" Knorr";
                // yB      if(tmp->MoverParameters->BrakeSubsystem==Oerlikon) OutText2+=" Oerlikon";
                // yB      if(tmp->MoverParameters->BrakeSubsystem==Hik) OutText2+=" Hik";
                // yB      if(tmp->MoverParameters->BrakeSubsystem==WeLu) OutText2+=" Łestinghałs";
                // OutText2= " GetFirst:
                // "+AnsiString(tmp->GetFirstDynamic(1)->MoverParameters->Name)+" Damage
                // status="+tmp->MoverParameters->EngineDescription(0)+" Engine status: ";
                // OutText2+= " GetLast:
                // "+AnsiString(tmp->GetLastDynamic(1)->MoverParameters->Name)+" Damage
                // status="+tmp->MoverParameters->EngineDescription(0)+" Engine status: ";
                OutText3 = ("BP: ") +
                           to_string(tmp->MoverParameters->BrakePress, 2) +
                           (", ");
                OutText3 += to_string(tmp->MoverParameters->BrakeStatus, 0) +
                            (", ");
                OutText3 += ("PP: ") +
                            to_string(tmp->MoverParameters->PipePress, 2) +
                            ("/");
                OutText3 += to_string(tmp->MoverParameters->ScndPipePress, 2) +
                            ("/");
                OutText3 += to_string(tmp->MoverParameters->EqvtPipePress, 2) +
                            (", ");
                OutText3 += ("BVP: ") +
                            to_string(tmp->MoverParameters->Volume, 3) +
                            (", ");
                OutText3 += to_string(tmp->MoverParameters->CntrlPipePress, 3) +
                            (", ");
                OutText3 += to_string(tmp->MoverParameters->Hamulec->GetCRP(), 3) +
                            (", ");
                OutText3 += to_string(tmp->MoverParameters->BrakeStatus, 0) +
                            (", ");
                //      OutText3+=AnsiString("BVP:
                //      ")+FloatToStrF(tmp->MoverParameters->BrakeVP(),ffFixed,5,2)+AnsiString(",
                //      ");

                //      OutText3+=FloatToStrF(tmp->MoverParameters->CntrlPipePress,ffFixed,5,2)+AnsiString(",
                //      ");
                //      OutText3+=FloatToStrF(tmp->MoverParameters->HighPipePress,ffFixed,5,2)+AnsiString(",
                //      ");
                //      OutText3+=FloatToStrF(tmp->MoverParameters->LowPipePress,ffFixed,5,2)+AnsiString(",
                //      ");

                if (tmp->MoverParameters->ManualBrakePos > 0)
                    OutText3 += ("manual brake active. ");
                else if (tmp->MoverParameters->LocalBrakePos > 0)
                    OutText3 += ("local brake active. ");
                else
                    OutText3 += ("local brake inactive. ");
                /*
                       //OutText3+=AnsiString("LSwTim:
                   ")+FloatToStrF(tmp->MoverParameters->LastSwitchingTime,ffFixed,5,2);
                       //OutText3+=AnsiString(" Physic:
                   ")+FloatToStrF(tmp->MoverParameters->PhysicActivation,ffFixed,5,2);
                       //OutText3+=AnsiString(" ESF:
                   ")+FloatToStrF(tmp->MoverParameters->EndSignalsFlag,ffFixed,5,0);
                       OutText3+=AnsiString(" dPAngF: ")+FloatToStrF(tmp->dPantAngleF,ffFixed,5,0);
                       OutText3+=AnsiString(" dPAngFT:
                   ")+FloatToStrF(-(tmp->PantTraction1*28.9-136.938),ffFixed,5,0);
                       if (tmp->lastcabf==1)
                       {
                        OutText3+=AnsiString(" pcabc1:
                   ")+FloatToStrF(tmp->MoverParameters->PantFrontUp,ffFixed,5,0);
                        OutText3+=AnsiString(" pcabc2:
                   ")+FloatToStrF(tmp->MoverParameters->PantRearUp,ffFixed,5,0);
                       }
                       if (tmp->lastcabf==-1)
                       {
                        OutText3+=AnsiString(" pcabc1:
                   ")+FloatToStrF(tmp->MoverParameters->PantRearUp,ffFixed,5,0);
                        OutText3+=AnsiString(" pcabc2:
                   ")+FloatToStrF(tmp->MoverParameters->PantFrontUp,ffFixed,5,0);
                       }
                */
                OutText4 = "";
                if (tmp->Mechanik)
                { // o ile jest ktoś w środku
                    // OutText4=tmp->Mechanik->StopReasonText();
                    // if (!OutText4.IsEmpty()) OutText4+="; "; //aby ładniejszy odstęp był
                    // if (Controlled->Mechanik && (Controlled->Mechanik->AIControllFlag==AIdriver))
                    std::string flags = "bwaccmlshhhoibsgvdp; "; // flagi AI (definicja w Driver.h)
					for (int i = 0, j = 1; i < 19; ++i, j <<= 1)
						if (tmp->Mechanik->DrivigFlags() & j) // jak bit ustawiony
							flags[i + 1] = std::toupper(flags[i + 1]); // ^= 0x20; // to zmiana na wielką literę
                    OutText4 = flags;
					OutText4 +=
						("Driver: Vd=") +
						to_string(tmp->Mechanik->VelDesired, 0) + (" ad=") +
						to_string(tmp->Mechanik->AccDesired, 2) + (" Pd=") +
						to_string(tmp->Mechanik->ActualProximityDist, 0) +
						(" Vn=") + to_string(tmp->Mechanik->VelNext, 0) +
						(" VSm=") + to_string(tmp->Mechanik->VelSignalLast, 0) +
						(" VLm=") + to_string(tmp->Mechanik->VelLimitLast, 0) +
						(" VRd=") + to_string(tmp->Mechanik->VelRoad, 0);
                    if (tmp->Mechanik->VelNext == 0.0)
                        if (tmp->Mechanik->eSignNext)
                        { // jeśli ma zapamiętany event semafora
                            // if (!OutText4.IsEmpty()) OutText4+=", "; //aby ładniejszy odstęp był
                            OutText4 += " (" +
                                        Global::Bezogonkow(tmp->Mechanik->eSignNext->asName) +
                                        ")"; // nazwa eventu semafora
                        }
                }
                if (!OutText4.empty())
                    OutText4 += "; "; // aby ładniejszy odstęp był
                // informacja o sprzęgach nawet bez mechanika
                OutText4 +=
                    "C0=" + (tmp->PrevConnected ?
                                 tmp->PrevConnected->GetName() + ":" +
                                     to_string(tmp->MoverParameters->Couplers[0].CouplingFlag) :
                                 std::string("NULL"));
                OutText4 +=
                    " C1=" + (tmp->NextConnected ?
                                  tmp->NextConnected->GetName() + ":" +
                                      to_string(tmp->MoverParameters->Couplers[1].CouplingFlag) :
                                  std::string("NULL"));
                if (Console::Pressed(VK_F2))
                {
                    WriteLog(OutText1);
                    WriteLog(OutText2);
                    WriteLog(OutText3);
                    WriteLog(OutText4);
                }
            } // koniec treści podstawowego ekranu FK_V2
            else
            { // ekran drugi, czyli tabelka skanowania AI
                if (tmp->Mechanik) //żeby była tabelka, musi być AI
                { // tabelka jest na użytek testujących scenerie, więc nie musi być "ładna"
                    glColor3f(1.0f, 1.0f, 1.0f); // a, damy zielony. GF: jednak biały
                    // glTranslatef(0.0f,0.0f,-0.50f);
                    glRasterPos2f(-0.25f, 0.20f);
                    // OutText1="Scan distance: "+AnsiString(tmp->Mechanik->scanmax)+", back:
                    // "+AnsiString(tmp->Mechanik->scanback);
                    OutText1 = "Time: " + to_string((int)GlobalTime->hh) + ":";
                    int i = GlobalTime->mm; // bo inaczej potrafi zrobić "hh:010"
                    if (i < 10)
                        OutText1 += "0";
                    OutText1 += to_string(i); // minuty
                    OutText1 += ":";
                    i = floor(GlobalTime->mr); // bo inaczej potrafi zrobić "hh:mm:010"
                    if (i < 10)
                        OutText1 += "0";
                    OutText1 += to_string(i);
                    OutText1 +=
                        (". Vel: ") + to_string(tmp->GetVelocity(), 1);
                    OutText1 += ". Scan table:";
                    glPrint(Global::Bezogonkow(OutText1).c_str());
                    i = -1;
                    while ((OutText1 = tmp->Mechanik->TableText(++i)) != "")
                    { // wyświetlenie pozycji z tabelki
                        glRasterPos2f(-0.25f, 0.19f - 0.01f * i);
                        glPrint(Global::Bezogonkow(OutText1).c_str());
                    }
                    // podsumowanie sensu tabelki
                    OutText4 =
                        ("Driver: Vd=") +
                        to_string(tmp->Mechanik->VelDesired, 0) + (" ad=") +
                        to_string(tmp->Mechanik->AccDesired, 2) + (" Pd=") +
                        to_string(tmp->Mechanik->ActualProximityDist, 0) +
                        (" Vn=") + to_string(tmp->Mechanik->VelNext, 0) +
						("\n VSm=") + to_string(tmp->Mechanik->VelSignalLast, 0) +
						(" VLm=") + to_string(tmp->Mechanik->VelLimitLast, 0) +
						(" VRd=") + to_string(tmp->Mechanik->VelRoad, 0) +
						(" VSig=") + to_string(tmp->Mechanik->VelSignal, 0);
                    if (tmp->Mechanik->VelNext == 0.0)
                        if (tmp->Mechanik->eSignNext)
                        { // jeśli ma zapamiętany event semafora
                            // if (!OutText4.IsEmpty()) OutText4+=", "; //aby ładniejszy odstęp był
                            OutText4 += " (" +
                                        Global::Bezogonkow(tmp->Mechanik->eSignNext->asName) +
                                        ")"; // nazwa eventu semafora
                        }
                    glRasterPos2f(-0.25f, 0.19f - 0.01f * i);
                    glPrint(Global::Bezogonkow(OutText4).c_str());
                }
            } // koniec ekanu skanowania
        } // koniec obsługi, gdy mamy wskaźnik do pojazdu
        else
        { // wyświetlenie współrzędnych w scenerii oraz kąta kamery, gdy nie mamy wskaźnika
            OutText1 = "Camera position: " + to_string(Camera.Pos.x, 2) + " " +
                       to_string(Camera.Pos.y, 2) + " " +
                       to_string(Camera.Pos.z, 2);
            OutText1 += ", azimuth: " +
                        to_string(180.0 - RadToDeg(Camera.Yaw), 0); // ma być azymut, czyli 0 na północy i rośnie na wschód
            OutText1 +=
                " " +
                std::string("S SEE NEN NWW SW")
                    .substr(1 + 2 * floor(fmod(8 + (Camera.Yaw + 0.5 * M_PI_4) / M_PI_4, 8)), 2);
        }
        // OutText3= AnsiString("  Online documentation (PL, ENG, DE, soon CZ):
        // http://www.eu07.pl");
        // OutText3="enrot="+FloatToStrF(Controlled->MoverParameters->enrot,ffFixed,6,2);
        // OutText3="; n="+FloatToStrF(Controlled->MoverParameters->n,ffFixed,6,2);
    } // koniec treści podstawowego ekranu FK_V2
    else if (Global::iTextMode == VK_F5)
    { // przesiadka do innego pojazdu
        if (FreeFlyModeFlag) // jeśli tryb latania
        {
            TDynamicObject *tmp = Ground.DynamicNearest(Camera.Pos, 50, true); //łapiemy z obsadą
            if (tmp)
                if (tmp != Controlled)
                {
                    if (Controlled) // jeśli mielismy pojazd
                        if (Controlled->Mechanik) // na skutek jakiegoś błędu może czasem zniknąć
                            Controlled->Mechanik->TakeControl(true); // oddajemy dotychczasowy AI
                    if (DebugModeFlag ? true : tmp->MoverParameters->Vel <= 5.0)
                    {
                        Controlled = tmp; // przejmujemy nowy
                        mvControlled = Controlled->ControlledFind()->MoverParameters;
                        if (Train)
                            Train->Silence(); // wyciszenie dźwięków opuszczanego pojazdu
                        else
                            Train = new TTrain(); // jeśli niczym jeszcze nie jeździlismy
                        if (Train->Init(Controlled))
                        { // przejmujemy sterowanie
                            if (!DebugModeFlag) // w DebugMode nadal prowadzi AI
                                Controlled->Mechanik->TakeControl(false);
                        }
                        else
                            SafeDelete(Train); // i nie ma czym sterować
                        // Global::pUserDynamic=Controlled; //renerowanie pojazdu względem kabiny
                        // Global::iTextMode=VK_F4;
                        if (Train)
                            InOutKey(); // do kabiny
                    }
                }
            Global::iTextMode = 0; // tryb neutralny
        }
        /*

             OutText1=OutText2=OutText3=OutText4="";
             AnsiString flag[10]={"vmax", "tory", "smfr", "pjzd", "mnwr", "pstk", "brak", "brak",
           "brak", "brak"};
             if(tmp)
             if(tmp->Mechanik)
             {
              for(int i=0;i<15;i++)
              {
               int tmppar=floor(tmp->Mechanik->ProximityTable[i].Vel);
               OutText2+=(tmppar<1000?(tmppar<100?((tmppar<10)&&(tmppar>=0)?"   ":"  "):"
           "):"")+IntToStr(tmppar)+" ";
               tmppar=floor(tmp->Mechanik->ProximityTable[i].Dist);
               OutText3+=(tmppar<1000?(tmppar<100?((tmppar<10)&&(tmppar>=0)?"   ":"  "):"
           "):"")+IntToStr(tmppar)+" ";
               OutText1+=flag[tmp->Mechanik->ProximityTable[i].Flag]+" ";
              }
              for(int i=0;i<6;i++)
              {
               int tmppar=floor(tmp->Mechanik->ReducedTable[i]);
               OutText4+=flag[i]+":"+(tmppar<1000?(tmppar<100?((tmppar<10)&&(tmppar>=0)?"   ":"
           "):" "):"")+IntToStr(tmppar)+" ";
              }
             }
        */
    }
    else if (Global::iTextMode == VK_F10)
    { // tu mozna dodac dopisywanie do logu przebiegu lokomotywy
        // Global::iViewMode=VK_F10;
        // return false;
        OutText1 = ("To quit press [Y] key.");
        OutText3 = ("Aby zakonczyc program, przycisnij klawisz [Y].");
    }
    else if (Controlled && DebugModeFlag && !Global::iTextMode)
    {
        OutText1 += (";  vel ") + to_string(Controlled->GetVelocity(), 2);
        OutText1 += (";  pos ") + to_string(Controlled->GetPosition().x, 2);
        OutText1 += (" ; ") + to_string(Controlled->GetPosition().y, 2);
        OutText1 += (" ; ") + to_string(Controlled->GetPosition().z, 2);
        OutText1 += ("; dist=") + to_string(Controlled->MoverParameters->DistCounter, 4);

        // double a= acos( DotProduct(Normalize(Controlled->GetDirection()),vWorldFront));
        //      OutText+= AnsiString(";   angle ")+FloatToStrF(a/M_PI*180,ffFixed,6,2);
        OutText1 +=
            ("; d_omega ") + to_string(Controlled->MoverParameters->dizel_engagedeltaomega, 3);
        OutText2 = ("HamZ=") + to_string(Controlled->MoverParameters->fBrakeCtrlPos, 1);
        OutText2 += ("; HamP=") + to_string(mvControlled->LocalBrakePos);
        OutText2 += ("/") + to_string(Controlled->MoverParameters->LocalBrakePosA, 2);
        // mvControlled->MainCtrlPos;
        // if (mvControlled->MainCtrlPos<0)
        //    OutText2+= AnsiString("; nastawnik 0");
        //      if (mvControlled->MainCtrlPos>iPozSzereg)
        OutText2 += ("; NasJ=") + to_string(mvControlled->MainCtrlPos);
        //      else
        //          OutText2+= AnsiString("; nastawnik S") + mvControlled->MainCtrlPos;
        OutText2 += ("(") + to_string(mvControlled->MainCtrlActualPos);

        OutText2 += ("); NasB=") + to_string(mvControlled->ScndCtrlPos);
        OutText2 += ("(") + to_string(mvControlled->ScndCtrlActualPos);
        if (mvControlled->TrainType == dt_EZT)
            OutText2 += ("); I=") + to_string(int(mvControlled->ShowCurrent(0)));
        else
            OutText2 += ("); I=") + to_string(int(mvControlled->Im));
        // OutText2+=AnsiString(";
        // I2=")+FloatToStrF(Controlled->NextConnected->MoverParameters->Im,ffFixed,6,2);
        OutText2 += ("; U=") +
                    to_string(int(mvControlled->RunningTraction.TractionVoltage + 0.5));
        // OutText2+=AnsiString("; rvent=")+FloatToStrF(mvControlled->RventRot,ffFixed,6,2);
        OutText2 += ("; R=") +
                    to_string(Controlled->MoverParameters->RunningShape.R, 1);
        OutText2 += (" An=") + to_string(Controlled->MoverParameters->AccN,                                                      2); // przyspieszenie poprzeczne
		if (tprev != int(GlobalTime->mr))
		{
			tprev = GlobalTime->mr;
			Acc = (Controlled->MoverParameters->Vel - VelPrev) / 3.6;
			VelPrev = Controlled->MoverParameters->Vel;
		}
		OutText2 += ("; As=") + to_string(Acc/*Controlled->MoverParameters->AccS*/, 2); // przyspieszenie wzdłużne
        // OutText2+=AnsiString("; P=")+FloatToStrF(mvControlled->EnginePower,ffFixed,6,1);
        OutText3 += ("cyl.ham. ") +
                    to_string(Controlled->MoverParameters->BrakePress, 2);
        OutText3 += ("; prz.gl. ") +
                    to_string(Controlled->MoverParameters->PipePress, 2);
        OutText3 += ("; zb.gl. ") +
                    to_string(Controlled->MoverParameters->CompressedVolume, 2);
        // youBy - drugi wezyk
        OutText3 += ("; p.zas. ") +
                    to_string(Controlled->MoverParameters->ScndPipePress, 2);

		if (Controlled->MoverParameters->EngineType == ElectricInductionMotor)
		{
			// glTranslatef(0.0f,0.0f,-0.50f);
			glColor3f(1.0f, 1.0f, 1.0f); // a, damy białym
			for (int i = 0; i <= 20; i++)
			{
				glRasterPos2f(-0.25f, 0.16f - 0.01f * i);
				if (Controlled->MoverParameters->eimc[i] < 10)
					OutText4 = to_string(Controlled->MoverParameters->eimc[i], 3);
				else
					OutText4 = to_string(Controlled->MoverParameters->eimc[i], 3);
				glPrint(OutText4.c_str());
			}
			for (int i = 0; i <= 20; i++)
			{
				glRasterPos2f(-0.2f, 0.16f - 0.01f * i);
				if (Controlled->MoverParameters->eimv[i] < 10)
					OutText4 = to_string(Controlled->MoverParameters->eimv[i], 3);
				else
					OutText4 = to_string(Controlled->MoverParameters->eimv[i], 3);
				glPrint(OutText4.c_str());
			}
			for (int i = 0; i <= 10; i++)
			{
				glRasterPos2f(-0.15f, 0.16f - 0.01f * i);
				OutText4 = to_string(Train->fPress[i][0], 3);
				glPrint(OutText4.c_str());
			}
			for (int i = 0; i <= 8; i++)
			{
				glRasterPos2f(-0.15f, 0.04f - 0.01f * i);
				OutText4 = to_string(Controlled->MED[0][i], 3);
				glPrint(OutText4.c_str());
			}
			for (int i = 0; i <= 8; i++)
			{
				for (int j = 0; j <= 9; j++)
				{
					glRasterPos2f(0.05f + 0.03f * i, 0.16f - 0.01f * j);
					OutText4 = to_string(Train->fEIMParams[i][j], 2);
					glPrint(OutText4.c_str());
				}
			}
			OutText4 = "";
			// glTranslatef(0.0f,0.0f,+0.50f);
			glColor3f(1.0f, 0.0f, 0.0f); // a, damy czerwonym
		}

        // ABu: testy sprzegow-> (potem przeniesc te zmienne z public do protected!)
        // OutText3+=AnsiString("; EnginePwr=")+FloatToStrF(mvControlled->EnginePower,ffFixed,1,5);
        // OutText3+=AnsiString("; nn=")+FloatToStrF(Controlled->NextConnectedNo,ffFixed,1,0);
        // OutText3+=AnsiString("; PR=")+FloatToStrF(Controlled->dPantAngleR,ffFixed,3,0);
        // OutText3+=AnsiString("; PF=")+FloatToStrF(Controlled->dPantAngleF,ffFixed,3,0);
        // if(Controlled->bDisplayCab==true)
        // OutText3+=AnsiString("; Wysw. kab");//+Controlled->mdKabina->GetSMRoot()->Name;
        // else
        // OutText3+=AnsiString("; test:")+AnsiString(Controlled->MoverParameters->TrainType[1]);

        // OutText3+=FloatToStrF(Train->Dynamic()->MoverParameters->EndSignalsFlag,ffFixed,3,0);;

        // OutText3+=FloatToStrF(Train->Dynamic()->MoverParameters->EndSignalsFlag&byte(((((1+Train->Dynamic()->MoverParameters->CabNo)/2)*30)+2)),ffFixed,3,0);;

        // OutText3+=AnsiString(";
        // Ftmax=")+FloatToStrF(Controlled->MoverParameters->Ftmax,ffFixed,3,0);
        // OutText3+=AnsiString(";
        // FTotal=")+FloatToStrF(Controlled->MoverParameters->FTotal/1000.0f,ffFixed,3,2);
        // OutText3+=AnsiString(";
        // FTrain=")+FloatToStrF(Controlled->MoverParameters->FTrain/1000.0f,ffFixed,3,2);
        // Controlled->mdModel->GetSMRoot()->SetTranslate(vector3(0,1,0));

        // McZapkie: warto wiedziec w jakim stanie sa przelaczniki
        if (mvControlled->ConvOvldFlag)
            OutText3 += " C! ";
        else if (mvControlled->FuseFlag)
            OutText3 += " F! ";
        else if (!mvControlled->Mains)
            OutText3 += " () ";
        else
            switch (mvControlled->ActiveDir * (mvControlled->Imin == mvControlled->IminLo ? 1 : 2))
            {
            case 2:
            {
                OutText3 += " >> ";
                break;
            }
            case 1:
            {
                OutText3 += " -> ";
                break;
            }
            case 0:
            {
                OutText3 += " -- ";
                break;
            }
            case -1:
            {
                OutText3 += " <- ";
                break;
            }
            case -2:
            {
                OutText3 += " << ";
                break;
            }
            }
        // OutText3+=AnsiString("; dpLocal
        // ")+FloatToStrF(Controlled->MoverParameters->dpLocalValve,ffFixed,10,8);
        // OutText3+=AnsiString("; dpMain
        // ")+FloatToStrF(Controlled->MoverParameters->dpMainValve,ffFixed,10,8);
        // McZapkie: predkosc szlakowa
        if (Controlled->MoverParameters->RunningTrack.Velmax == -1)
        {
            OutText3 += (" Vtrack=Vmax");
        }
        else
        {
            OutText3 +=
                (" Vtrack ") +
                to_string(Controlled->MoverParameters->RunningTrack.Velmax, 2);
        }
        //      WriteLog(Controlled->MoverParameters->TrainType.c_str());
        if ((mvControlled->EnginePowerSource.SourceType == CurrentCollector) ||
            (mvControlled->TrainType == dt_EZT))
        {
            OutText3 +=
                ("; pant. ") + to_string(mvControlled->PantPress, 2);
            OutText3 += (mvControlled->bPantKurek3 ? "=ZG" : "|ZG");
        }
        // McZapkie: komenda i jej parametry
        if (Controlled->MoverParameters->CommandIn.Command != (""))
            OutText4 = ("C:") +
                       (Controlled->MoverParameters->CommandIn.Command) +
                       (" V1=") +
                       to_string(Controlled->MoverParameters->CommandIn.Value1, 0) +
                       (" V2=") +
                       to_string(Controlled->MoverParameters->CommandIn.Value2, 0);
        if (Controlled->Mechanik && (Controlled->Mechanik->AIControllFlag == AIdriver))
            OutText4 +=
                ("AI: Vd=") +
                to_string(Controlled->Mechanik->VelDesired, 0) + (" ad=") +
                to_string(Controlled->Mechanik->AccDesired, 2) + (" Pd=") +
                to_string(Controlled->Mechanik->ActualProximityDist, 0) +
                (" Vn=") + to_string(Controlled->Mechanik->VelNext, 0);
    }

    // ABu 150205: prosty help, zeby sie na forum nikt nie pytal, jak ma ruszyc :)

    if (Global::detonatoryOK)
    {
        // if (Console::Pressed(VK_F9)) ShowHints(); //to nie działa prawidłowo - prosili wyłączyć
        if (Global::iTextMode == VK_F9)
        { // informacja o wersji, sposobie wyświetlania i błędach OpenGL
            // Global::iViewMode=VK_F9;
            OutText1 = Global::asVersion; // informacja o wersji
            OutText2 = std::string("Rendering mode: ") + (Global::bUseVBO ? "VBO" : "Display Lists");
            if (Global::iMultiplayer)
                OutText2 += ". Multiplayer is active";
            OutText2 += ".";
            GLenum err = glGetError();
            if (err != GL_NO_ERROR)
            {
                OutText3 = "OpenGL error " + to_string(err) + ": " +
                           Global::Bezogonkow(((char *)gluErrorString(err)));
            }
        }
        if (Global::iTextMode == VK_F3)
        { // wyświetlenie rozkładu jazdy, na razie jakkolwiek
            TDynamicObject *tmp = FreeFlyModeFlag ?
                                      Ground.DynamicNearest(Camera.Pos) :
                                      Controlled; // w trybie latania lokalizujemy wg mapy
            Mtable::TTrainParameters *tt = NULL;
            if (tmp)
                if (tmp->Mechanik)
                {
                    tt = tmp->Mechanik->Timetable();
                    if (tt) // musi być rozkład
                    { // wyświetlanie rozkładu
                        glColor3f(1.0f, 1.0f, 1.0f); // a, damy białym
                        // glTranslatef(0.0f,0.0f,-0.50f);
                        glRasterPos2f(-0.25f, 0.20f);
                        OutText1 = tmp->Mechanik->Relation() + " (" +
                                   tmp->Mechanik->Timetable()->TrainName + ")";
                        glPrint(Global::Bezogonkow(OutText1, true).c_str());
                        glRasterPos2f(-0.25f, 0.19f);
                        // glPrint("|============================|=======|=======|=====|");
                        // glPrint("| Posterunek                 | Przyj.| Odjazd| Vmax|");
                        // glPrint("|============================|=======|=======|=====|");
                        glPrint("|----------------------------|-------|-------|-----|");
                        TMTableLine *t;
                        for (int i = tmp->Mechanik->iStationStart; i <= tt->StationCount; ++i)
                        { // wyświetlenie pozycji z rozkładu
                            t = tt->TimeTable + i; // linijka rozkładu
                            OutText1 = (t->StationName +
                                                  "                          ").substr(0, 26);
                            OutText2 = (t->Ah >= 0) ?
                                           to_string(int(100 + t->Ah)).substr(1, 2) + ":" +
                                               to_string(int(100 + t->Am)).substr(1, 2) :
                                           std::string("     ");
                            OutText3 = (t->Dh >= 0) ?
                                           to_string(int(100 + t->Dh)).substr(1, 2) + ":" +
                                               to_string(int(100 + t->Dm)).substr(1, 2) :
                                           std::string("     ");
                            OutText4 = "   " + to_string(t->vmax, 0);
                            OutText4 = OutText4.substr(OutText4.length() - 3,
                                                          3); // z wyrównaniem do prawej
                            // if (AnsiString(t->StationWare).Pos("@"))
                            OutText1 = "| " + OutText1 + " | " + OutText2 + " | " + OutText3 +
                                       " | " + OutText4 + " | " + t->StationWare;
                            glRasterPos2f(-0.25f,
                                          0.18f - 0.02f * (i - tmp->Mechanik->iStationStart));
                            if ((tmp->Mechanik->iStationStart < tt->StationIndex) ?
                                    (i < tt->StationIndex) :
                                    false)
                            { // czas minął i odjazd był, to nazwa stacji będzie na zielono
                                glColor3f(0.0f, 1.0f, 0.0f); // zielone
                                glRasterPos2f(
                                    -0.25f,
                                    0.18f - 0.02f * (i - tmp->Mechanik->iStationStart)); // dopiero
                                // ustawienie
                                // pozycji
                                // ustala
                                // kolor,
                                // dziwne...
                                glPrint(Global::Bezogonkow(OutText1, true).c_str());
                                glColor3f(1.0f, 1.0f, 1.0f); // a reszta białym
                            }
                            else // normalne wyświetlanie, bez zmiany kolorów
                                glPrint(Global::Bezogonkow(OutText1, true).c_str());
                            glRasterPos2f(-0.25f,
                                          0.17f - 0.02f * (i - tmp->Mechanik->iStationStart));
                            glPrint("|----------------------------|-------|-------|-----|");
                        }
                    }
                }
            OutText1 = OutText2 = OutText3 = OutText4 = "";
        }
        else if (OutText1 != "")
        { // ABu: i od razu czyszczenie tego, co bylo napisane
            // glTranslatef(0.0f,0.0f,-0.50f);
            glRasterPos2f(-0.25f, 0.20f);
            glPrint(OutText1.c_str());
            OutText1 = "";
            if (OutText2 != "")
            {
                glRasterPos2f(-0.25f, 0.19f);
                glPrint(OutText2.c_str());
                OutText2 = "";
            }
            if (OutText3 != "")
            {
                glRasterPos2f(-0.25f, 0.18f);
                glPrint(OutText3.c_str());
                OutText3 = "";
                if (OutText4 != "")
                {
                    glRasterPos2f(-0.25f, 0.17f);
                    glPrint(OutText4.c_str());
                    OutText4 = "";
                }
            }
        }
        // if ((Global::iTextMode!=VK_F3))
        { // stenogramy dźwięków (ukryć, gdy tabelka skanowania lub rozkład?)
            glColor3f(1.0f, 1.0f, 0.0f); //żółte
            for (int i = 0; i < 5; ++i)
            { // kilka linijek
                if (Global::asTranscript[i].empty())
                    break; // dalej nie trzeba
                else
                {
                    glRasterPos2f(-0.20f, -0.05f - 0.01f * i);
                    glPrint(Global::Bezogonkow(Global::asTranscript[i]).c_str());
                }
            }
        }
    }
    // if (Global::iViewMode!=Global::iTextMode)
    //{//Ra: taka maksymalna prowizorka na razie
    // WriteLog("Pressed function key F"+AnsiString(Global::iViewMode-111));
    // Global::iTextMode=Global::iViewMode;
    //}
    glEnable(GL_LIGHTING);
    return (true);
};

bool TWorld::Render()
{
    glColor3b(255, 255, 255);
    // glColor3b(255, 0, 255);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_MODELVIEW); // Select The Modelview Matrix
    glLoadIdentity();
    Camera.SetMatrix(); // ustawienie macierzy kamery względem początku scenerii
    glLightfv(GL_LIGHT0, GL_POSITION, Global::lightPos);

    if (!Global::bWireFrame)
    { // bez nieba w trybie rysowania linii
        glDisable(GL_FOG);
        Clouds.Render();
        glEnable(GL_FOG);
    }
    if (Global::bUseVBO)
    { // renderowanie przez VBO
        if (!Ground.RenderVBO(Camera.Pos))
            return false;
        if (!Ground.RenderAlphaVBO(Camera.Pos))
            return false;
    }
    else
    { // renderowanie przez Display List
        if (!Ground.RenderDL(Camera.Pos))
            return false;
        if (!Ground.RenderAlphaDL(Camera.Pos))
            return false;
    }
    TSubModel::iInstance = (int)(Train ? Train->Dynamic() : 0); //żeby nie robić cudzych animacji
    // if (Camera.Type==tp_Follow)
    if (Train)
        Train->Update();
    // if (Global::bRenderAlpha)
    // if (Controlled)
    //  Train->RenderAlpha();
    glFlush();
    // Global::bReCompile=false; //Ra: już zrobiona rekompilacja
    ResourceManager::Sweep(Timer::GetSimulationTime());
    return true;
};

void TWorld::ShowHints(void)
{ // Ra: nie używać tego, bo źle działa
    glBindTexture(GL_TEXTURE_2D, 0);
    glColor4f(0.3f, 1.0f, 0.3f, 1.0f);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -0.50f);
    // glRasterPos2f(-0.25f, 0.20f);
    // OutText1="Uruchamianie lokomotywy - pomoc dla niezaawansowanych";
    // glPrint(OutText1.c_str());

    // if(TestFlag(Controlled->MoverParameters->SecuritySystem.Status,s_ebrake))
    // hunter-091012
    if (TestFlag(Controlled->MoverParameters->SecuritySystem.Status, s_SHPebrake) ||
        TestFlag(Controlled->MoverParameters->SecuritySystem.Status, s_CAebrake))
    {
        OutText1 = "Gosciu, ale refleks to ty masz szachisty. Teraz zaczekaj.";
        OutText2 = "W tej sytuacji czuwak mozesz zbic dopiero po zatrzymaniu pociagu. ";
        if (Controlled->MoverParameters->Vel == 0)
            OutText3 = "   (mozesz juz nacisnac spacje)";
    }
    else
        // if(TestFlag(Controlled->MoverParameters->SecuritySystem.Status,s_alarm))
        if (TestFlag(Controlled->MoverParameters->SecuritySystem.Status, s_CAalarm) ||
            TestFlag(Controlled->MoverParameters->SecuritySystem.Status, s_SHPalarm))
    {
        OutText1 = "Natychmiast zbij czuwak, bo pociag sie zatrzyma!";
        OutText2 = "   (szybko nacisnij spacje!)";
    }
    else if (TestFlag(Controlled->MoverParameters->SecuritySystem.Status, s_aware))
    {
        OutText1 = "Zbij czuwak, zeby udowodnic, ze nie spisz :) ";
        OutText2 = "   (nacisnij spacje)";
    }
    else if (mvControlled->FuseFlag)
    {
        OutText1 = "Czlowieku, delikatniej troche! Gdzie sie spieszysz?";
        OutText2 = "Wybilo Ci bezpiecznik nadmiarowy, teraz musisz wlaczyc go ponownie.";
        OutText3 = "   ('N', wczesniej nastawnik i boczniki na zero -> '-' oraz '*' do oporu)";
    }
    else if (mvControlled->V == 0)
    {
        if ((mvControlled->PantFrontVolt == 0.0) || (mvControlled->PantRearVolt == 0.0))
        {
            OutText1 = "Jezdziles juz kiedys lokomotywa? Pierwszy raz? Dobra, to zaczynamy.";
            OutText2 = "No to co, trzebaby chyba podniesc pantograf?";
            OutText3 = "   (wcisnij 'shift+P' - przedni, 'shift+O' - tylny)";
        }
        else if (!mvControlled->Mains)
        {
            OutText1 = "Dobra, mozemy zalaczyc wylacznik szybki lokomotywy.";
            OutText2 = "   (wcisnij 'shift+M')";
        }
        else if (!mvControlled->ConverterAllow)
        {
            OutText1 = "Teraz wlacz przetwornice.";
            OutText2 = "   (wcisnij 'shift+X')";
        }
        else if (!mvControlled->CompressorAllow)
        {
            OutText1 = "Teraz wlacz sprezarke.";
            OutText2 = "   (wcisnij 'shift+C')";
        }
        else if (mvControlled->ActiveDir == 0)
        {
            OutText1 = "Ustaw nastawnik kierunkowy na kierunek, w ktorym chcesz jechac.";
            OutText2 = "   ('d' - do przodu, 'r' - do tylu)";
        }
        else if (Controlled->GetFirstDynamic(1)->MoverParameters->BrakePress > 0)
        {
            OutText1 = "Odhamuj sklad i zaczekaj az Ci powiem - to moze troche potrwac.";
            OutText2 = "   ('.' na klawiaturze numerycznej)";
        }
        else if (Controlled->MoverParameters->BrakeCtrlPos != 0)
        {
            OutText1 = "Przelacz kran hamulca w pozycje 'jazda'.";
            OutText2 = "   ('4' na klawiaturze numerycznej)";
        }
        else if (mvControlled->MainCtrlPos == 0)
        {
            OutText1 = "Teraz juz mozesz ruszyc ustawiajac pierwsza pozycje na nastawniku jazdy.";
            OutText2 = "   (jeden raz '+' na klawiaturze numerycznej)";
        }
        else if ((mvControlled->MainCtrlPos > 0) && (mvControlled->ShowCurrent(1) != 0))
        {
            OutText1 = "Dobrze, mozesz teraz wlaczac kolejne pozycje nastawnika.";
            OutText2 = "   ('+' na klawiaturze numerycznej, tylko z wyczuciem)";
        }
        if ((mvControlled->MainCtrlPos > 1) && (mvControlled->ShowCurrent(1) == 0))
        {
            OutText1 = "Spieszysz sie gdzies? Zejdz nastawnikiem na zero i probuj jeszcze raz!";
            OutText2 = "   (teraz do oporu '-' na klawiaturze numerycznej)";
        }
    }
    else
    {
        OutText1 = "Aby przyspieszyc mozesz wrzucac kolejne pozycje nastawnika.";
        if (mvControlled->MainCtrlPos == 28)
        {
            OutText1 = "Przy tym ustawienu mozesz bocznikowac silniki - sprobuj: '/' i '*' ";
        }
        if (mvControlled->MainCtrlPos == 43)
        {
            OutText1 = "Przy tym ustawienu mozesz bocznikowac silniki - sprobuj: '/' i '*' ";
        }

        OutText2 = "Aby zahamowac zejdz nastawnikiem do 0 ('-' do oporu) i ustaw kran hamulca";
        OutText3 =
            "w zaleznosci od sily hamowania, jakiej potrzebujesz ('2', '5' lub '8' na kl. num.)";

        // else
        // if() OutText1="teraz mozesz ruszyc naciskajac jeden raz '+' na klawiaturze numerycznej";
        // else
        // if() OutText1="teraz mozesz ruszyc naciskajac jeden raz '+' na klawiaturze numerycznej";
    }
    // OutText3=FloatToStrF(Controlled->MoverParameters->SecuritySystem.Status,ffFixed,3,0);

    if (OutText1 != "")
    {
        glRasterPos2f(-0.25f, 0.19f);
        glPrint(OutText1.c_str());
        OutText1 = "";
    }
    if (OutText2 != "")
    {
        glRasterPos2f(-0.25f, 0.18f);
        glPrint(OutText2.c_str());
        OutText2 = "";
    }
    if (OutText3 != "")
    {
        glRasterPos2f(-0.25f, 0.17f);
        glPrint(OutText3.c_str());
        OutText3 = "";
    }
};

//---------------------------------------------------------------------------
void TWorld::OnCommandGet(DaneRozkaz *pRozkaz)
{ // odebranie komunikatu z serwera
    if (pRozkaz->iSygn == 'EU07')
        switch (pRozkaz->iComm)
        {
        case 0: // odesłanie identyfikatora wersji
			CommLog( Now() + " " + std::to_string(pRozkaz->iComm) + " version" + " rcvd");
			Ground.WyslijString(Global::asVersion, 0); // przedsatwienie się
            break;
        case 1: // odesłanie identyfikatora wersji
			CommLog( Now() + " " + std::to_string(pRozkaz->iComm) + " scenery" + " rcvd");
			Ground.WyslijString(Global::SceneryFile, 1); // nazwa scenerii
            break;
        case 2: // event
            CommLog( Now() + " " + std::to_string(pRozkaz->iComm) + " " +
                    std::string(pRozkaz->cString + 1, (unsigned)(pRozkaz->cString[0])) + " rcvd");
            if (Global::iMultiplayer)
            { // WriteLog("Komunikat: "+AnsiString(pRozkaz->Name1));
                TEvent *e = Ground.FindEvent(
                    std::string(pRozkaz->cString + 1, (unsigned)(pRozkaz->cString[0])));
                if (e)
                    if ((e->Type == tp_Multiple) || (e->Type == tp_Lights) ||
                        bool(e->evJoined)) // tylko jawne albo niejawne Multiple
                        Ground.AddToQuery(e, NULL); // drugi parametr to dynamic wywołujący - tu
                // brak
            }
            break;
        case 3: // rozkaz dla AI
            if (Global::iMultiplayer)
            {
                int i =
                    int(pRozkaz->cString[8]); // długość pierwszego łańcucha (z przodu dwa floaty)
                CommLog(
                    to_string(BorlandTime()) + " " + to_string(pRozkaz->iComm) + " " +
                    std::string(pRozkaz->cString + 11 + i, (unsigned)(pRozkaz->cString[10 + i])) +
                    " rcvd");
                TGroundNode *t = Ground.DynamicFind(
                    std::string(pRozkaz->cString + 11 + i,
                               (unsigned)pRozkaz->cString[10 + i])); // nazwa pojazdu jest druga
                if (t)
                    if (t->DynamicObject->Mechanik)
                    {
                        t->DynamicObject->Mechanik->PutCommand(std::string(pRozkaz->cString + 9, i),
                                                               pRozkaz->fPar[0], pRozkaz->fPar[1],
                                                               NULL, stopExt); // floaty są z przodu
                        WriteLog("AI command: " + std::string(pRozkaz->cString + 9, i));
                    }
            }
            break;
        case 4: // badanie zajętości toru
        {
            CommLog(to_string(BorlandTime()) + " " + to_string(pRozkaz->iComm) + " " +
                    std::string(pRozkaz->cString + 1, (unsigned)(pRozkaz->cString[0])) + " rcvd");
            TGroundNode *t = Ground.FindGroundNode(
                std::string(pRozkaz->cString + 1, (unsigned)(pRozkaz->cString[0])), TP_TRACK);
            if (t)
                if (t->pTrack->IsEmpty())
                    Ground.WyslijWolny(t->asName);
        }
        break;
        case 5: // ustawienie parametrów
        {
            CommLog(to_string(BorlandTime()) + " " + to_string(pRozkaz->iComm) + " params " +
                    to_string(*pRozkaz->iPar) + " rcvd");
            if (*pRozkaz->iPar == 0) // sprawdzenie czasu
                if (*pRozkaz->iPar & 1) // ustawienie czasu
                {
                    double t = pRozkaz->fPar[1];
                    GlobalTime->dd = floor(t); // niby nie powinno być dnia, ale...
                    if (Global::fMoveLight >= 0)
                        Global::fMoveLight = t; // trzeba by deklinację Słońca przeliczyć
                    GlobalTime->hh = floor(24 * t) - 24.0 * GlobalTime->dd;
                    GlobalTime->mm =
                        floor(60 * 24 * t) - 60.0 * (24.0 * GlobalTime->dd + GlobalTime->hh);
                    GlobalTime->mr =
                        floor(60 * 60 * 24 * t) -
                        60.0 * (60.0 * (24.0 * GlobalTime->dd + GlobalTime->hh) + GlobalTime->mm);
                }
            if (*pRozkaz->iPar & 2)
            { // ustawienie flag zapauzowania
                Global::iPause = pRozkaz->fPar[2]; // zakładamy, że wysyłający wie, co robi
            }
        }
        break;
        case 6: // pobranie parametrów ruchu pojazdu
            if (Global::iMultiplayer)
            { // Ra 2014-12: to ma działać również dla pojazdów bez obsady
                CommLog(to_string(BorlandTime()) + " " + to_string(pRozkaz->iComm) + " " +
                        std::string(pRozkaz->cString + 1, (unsigned)(pRozkaz->cString[0])) +
                        " rcvd");
                if (pRozkaz->cString[0]) // jeśli długość nazwy jest niezerowa
                { // szukamy pierwszego pojazdu o takiej nazwie i odsyłamy parametry ramką #7
                    TGroundNode *t;
                    if (pRozkaz->cString[1] == '*')
                        t = Ground.DynamicFind(
                            Global::asHumanCtrlVehicle); // nazwa pojazdu użytkownika
                    else
                        t = Ground.DynamicFindAny(std::string(
                            pRozkaz->cString + 1, (unsigned)pRozkaz->cString[0])); // nazwa pojazdu
                    if (t)
                        Ground.WyslijNamiary(t); // wysłanie informacji o pojeździe
                }
                else
                { // dla pustego wysyłamy ramki 6 z nazwami pojazdów AI (jeśli potrzebne wszystkie,
                    // to rozpoznać np. "*")
                    Ground.DynamicList();
                }
            }
            break;
        case 8: // ponowne wysłanie informacji o zajętych odcinkach toru
			CommLog(to_string(BorlandTime()) + " " + to_string(pRozkaz->iComm) + " all busy track" + " rcvd");
			Ground.TrackBusyList();
            break;
        case 9: // ponowne wysłanie informacji o zajętych odcinkach izolowanych
			CommLog(to_string(BorlandTime()) + " " + to_string(pRozkaz->iComm) + " all busy isolated" + " rcvd");
			Ground.IsolatedBusyList();
            break;
        case 10: // badanie zajętości jednego odcinka izolowanego
            CommLog(to_string(BorlandTime()) + " " + to_string(pRozkaz->iComm) + " " +
                    std::string(pRozkaz->cString + 1, (unsigned)(pRozkaz->cString[0])) + " rcvd");
            Ground.IsolatedBusy(std::string(pRozkaz->cString + 1, (unsigned)(pRozkaz->cString[0])));
            break;
        case 11: // ustawienie parametrów ruchu pojazdu
            //    Ground.IsolatedBusy(AnsiString(pRozkaz->cString+1,(unsigned)(pRozkaz->cString[0])));
            break;
		case 12: // skrocona ramka parametrow pojazdow AI (wszystkich!!)
			CommLog(to_string(BorlandTime()) + " " + to_string(pRozkaz->iComm) + " obsadzone" + " rcvd");
			Ground.WyslijObsadzone();
			//    Ground.IsolatedBusy(AnsiString(pRozkaz->cString+1,(unsigned)(pRozkaz->cString[0])));
			break;
		case 13: // ramka uszkodzenia i innych stanow pojazdu, np. wylaczenie CA, wlaczenie recznego itd.
				 //            WriteLog("Przyszlo 13!");
				 //            WriteLog(pRozkaz->cString);
                    CommLog(to_string(BorlandTime()) + " " + to_string(pRozkaz->iComm) + " " +
                            std::string(pRozkaz->cString + 1, (unsigned)(pRozkaz->cString[0])) +
                            " rcvd");
                    if (pRozkaz->cString[1]) // jeśli długość nazwy jest niezerowa
                        { // szukamy pierwszego pojazdu o takiej nazwie i odsyłamy parametry ramką #13
				TGroundNode *t;
				if (pRozkaz->cString[2] == '*')
					t = Ground.DynamicFind(
						Global::asHumanCtrlVehicle); // nazwa pojazdu użytkownika
				else
					t = Ground.DynamicFindAny(
						std::string(pRozkaz->cString + 2,
							(unsigned)pRozkaz->cString[1])); // nazwa pojazdu
				if (t)
				{
					TDynamicObject *d = t->DynamicObject;
					while (d)
					{
						d->Damage(pRozkaz->cString[0]);
						d = d->Next(); // pozostałe też
					}
					d = t->DynamicObject->Prev();
					while (d)
					{
						d->Damage(pRozkaz->cString[0]);
						d = d->Prev(); // w drugą stronę też
					}
					Ground.WyslijUszkodzenia(t->asName, t->DynamicObject->MoverParameters->EngDmgFlag); // zwrot informacji o pojeździe
				}
			}
			//    Ground.IsolatedBusy(AnsiString(pRozkaz->cString+1,(unsigned)(pRozkaz->cString[0])));
			break;
		}
};

//---------------------------------------------------------------------------
void TWorld::ModifyTGA(const std::string &dir)
{ // rekurencyjna modyfikacje plików TGA
/*  TODO: implement version without Borland stuff
	TSearchRec sr;
    if (FindFirst(dir + "*.*", faDirectory | faArchive, sr) == 0)
    {
        do
        {
            if (sr.Name[1] != '.')
                if ((sr.Attr & faDirectory)) // jeśli katalog, to rekurencja
                    ModifyTGA(dir + sr.Name + "/");
                else if (sr.Name.LowerCase().SubString(sr.Name.Length() - 3, 4) == ".tga")
                    TTexturesManager::GetTextureID(NULL, NULL, AnsiString(dir + sr.Name).c_str());
        } while (FindNext(sr) == 0);
        FindClose(sr);
    }
*/
};
//---------------------------------------------------------------------------
std::string last; // zmienne używane w rekurencji
double shift = 0;
void TWorld::CreateE3D(std::string const &dir, bool dyn)
{ // rekurencyjna generowanie plików E3D

/* TODO: remove Borland file access stuff
    TTrack *trk;
    double at;
    TSearchRec sr;
    if (FindFirst(dir + "*.*", faDirectory | faArchive, sr) == 0)
    {
        do
        {
            if (sr.Name[1] != '.')
                if ((sr.Attr & faDirectory)) // jeśli katalog, to rekurencja
                    CreateE3D(dir + sr.Name + "\\", dyn);
                else if (dyn)
                {
                    if (sr.Name.LowerCase().SubString(sr.Name.Length() - 3, 4) == ".mmd")
                    {
                        // konwersja pojazdów będzie ułomna, bo nie poustawiają się animacje na
                        // submodelach określonych w MMD
                        // TModelsManager::GetModel(AnsiString(dir+sr.Name).c_str(),true);
                        if (last != dir)
                        { // utworzenie toru dla danego pojazdu
                            last = dir;
                            trk = TTrack::Create400m(1, shift);
                            shift += 10.0; // następny tor będzie deczko dalej, aby nie zabić FPS
                            at = 400.0;
                            // if (shift>1000) break; //bezpiecznik
                        }
                        TGroundNode *tmp = new TGroundNode();
                        tmp->DynamicObject = new TDynamicObject();
                        // Global::asCurrentTexturePath=dir; //pojazdy mają tekstury we własnych
                        // katalogach
                        at -= tmp->DynamicObject->Init(
                            "", string((dir.SubString(9, dir.Length() - 9)).c_str()), "none",
                            string(sr.Name.SubString(1, sr.Name.Length() - 4).c_str()), trk, at, "nobody", 0.0,
                            "none", 0.0, "", false, "");
                        // po wczytaniu CHK zrobić pętlę po ładunkach, aby każdy z nich skonwertować
                        AnsiString loads, load;
                        loads = AnsiString(tmp->DynamicObject->MoverParameters->LoadAccepted.c_str()); // typy ładunków
                        if (!loads.IsEmpty())
                        {
                            loads += ","; // przecinek na końcu
                            int i = loads.Pos(",");
                            while (i > 1)
                            { // wypadało by sprawdzić, czy T3D ładunku jest
                                load = loads.SubString(1, i - 1);
                                if (FileExists(dir + load + ".t3d")) // o ile jest plik ładunku, bo
                                    // inaczej nie ma to sensu
                                    if (!FileExists(
                                            dir + load +
                                            ".e3d")) // a nie ma jeszcze odpowiednika binarnego
                                        at -= tmp->DynamicObject->Init(
                                            "", dir.SubString(9, dir.Length() - 9).c_str(), "none",
                                            sr.Name.SubString(1, sr.Name.Length() - 4).c_str(), trk, at,
                                            "nobody", 0.0, "none", 1.0, load.c_str(), false, "");
                                loads.Delete(1, i); // usunięcie z następującym przecinkiem
                                i = loads.Pos(",");
                            }
                        }
                        if (tmp->DynamicObject->iCabs)
                        { // jeśli ma jakąkolwiek kabinę
                            delete Train;
                            Train = new TTrain();
                            if (tmp->DynamicObject->iCabs & 1)
                            {
                                tmp->DynamicObject->MoverParameters->ActiveCab = 1;
                                Train->Init(tmp->DynamicObject, true);
                            }
                            if (tmp->DynamicObject->iCabs & 4)
                            {
                                tmp->DynamicObject->MoverParameters->ActiveCab = -1;
                                Train->Init(tmp->DynamicObject, true);
                            }
                            if (tmp->DynamicObject->iCabs & 2)
                            {
                                tmp->DynamicObject->MoverParameters->ActiveCab = 0;
                                Train->Init(tmp->DynamicObject, true);
                            }
                        }
                        Global::asCurrentTexturePath =
                            (szTexturePath); // z powrotem defaultowa sciezka do tekstur
                    }
                }
                else if (sr.Name.LowerCase().SubString(sr.Name.Length() - 3, 4) == ".t3d")
                { // z modelami jest prościej
                    Global::asCurrentTexturePath = dir.c_str();
                    TModelsManager::GetModel(AnsiString(dir + sr.Name).c_str(), false);
                }
        } while (FindNext(sr) == 0);
        FindClose(sr);
    }
*/
};
//---------------------------------------------------------------------------
void TWorld::CabChange(TDynamicObject *old, TDynamicObject *now)
{ // ewentualna zmiana kabiny użytkownikowi
    if (Train)
        if (Train->Dynamic() == old)
            Global::changeDynObj = now; // uruchomienie protezy
};
//---------------------------------------------------------------------------
