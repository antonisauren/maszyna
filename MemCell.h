//---------------------------------------------------------------------------

#ifndef MemCellH
#define MemCellH

#include "Classes.h"
#include "dumb3d.h"
using namespace Math3D;
using namespace Mover;

class TMemCell
{
private:

protected:

public:
    double fValue1;
    double fValue2;
    char *szText;
//McZapkie-100302 - zeby nazwe toru na ktory jest Putcommand wysylane pamietac:
    AnsiString asTrackName;
    __fastcall TMemCell();
    __fastcall ~TMemCell();
    void __fastcall Init();
    void __fastcall UpdateValues(char *szNewText, double fNewValue1, double fNewValue2, int CheckMask);
    bool __fastcall Load(cParser *parser);
    void __fastcall PutCommand(TController *Mech, vector3 *Loc);
    bool __fastcall Compare(char *szTestText, double fTestValue1, double fTestValue2, int CheckMask);
    bool __fastcall Render();
};

//---------------------------------------------------------------------------
#endif
