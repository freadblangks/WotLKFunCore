/* WotLKFunCore is a custom fun server built for
* World of Warcarft game build 3.3.5b. (12345)
*
* Copyright (C) 2008-2016 JustACore.
* Copyright (C) 2008-2016 TrinityCore.
* Copyright (C) 2005-2016 MaNGOS.
*
* ==== Start of GNU GPL ====
*
* This file is free software; as a special exception the author gives
* unlimited permission to copy and/or distribute it, with or without
* modifications, as long as this notice is preserved.
*
* This program is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY, to the extent permitted by law; without even the
* implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
*
* ==== End of GNU GPL ====
*/

#include <sys/types.h>
#include "VMapFactory.h"
#include "VMapManager2.h"
#include "G3D/Table.h"

using namespace G3D;

namespace VMAP
{
    void chompAndTrim(std::string& str)
    {
        while (str.length() >0)
        {
            char lc = str[str.length()-1];
            if (lc == '\r' || lc == '\n' || lc == ' ' || lc == '"' || lc == '\'')
            {
                str = str.substr(0, str.length()-1);
            }
            else
            {
                break;
            }
        }
        while (str.length() >0)
        {
            char lc = str[0];
            if (lc == ' ' || lc == '"' || lc == '\'')
            {
                str = str.substr(1, str.length()-1);
            }
            else
            {
                break;
            }
        }
    }

    IVMapManager* gVMapManager = 0;
    Table<unsigned int, bool>* iIgnoreSpellIds=0;

    //===============================================
    // result false, if no more id are found

    bool getNextId(const std::string& pString, unsigned int& pStartPos, unsigned int& pId)
    {
        bool result = false;
        unsigned int i;
        for (i=pStartPos;i<pString.size(); ++i)
        {
            if (pString[i] == ',')
            {
                break;
            }
        }
        if (i>pStartPos)
        {
            std::string idString = pString.substr(pStartPos, i-pStartPos);
            pStartPos = i+1;
            chompAndTrim(idString);
            pId = atoi(idString.c_str());
            result = true;
        }
        return(result);
    }

    //===============================================
    /**
    parameter: String of map ids. Delimiter = ","
    */

    void VMapFactory::preventSpellsFromBeingTestedForLoS(const char* pSpellIdString)
    {
        if (!iIgnoreSpellIds)
            iIgnoreSpellIds = new Table<unsigned int, bool>();
        if (pSpellIdString != NULL)
        {
            unsigned int pos =0;
            unsigned int id;
            std::string confString(pSpellIdString);
            chompAndTrim(confString);
            while (getNextId(confString, pos, id))
            {
                iIgnoreSpellIds->set(id, true);
            }
        }
    }

    //===============================================

    bool VMapFactory::checkSpellForLoS(unsigned int pSpellId)
    {
        return(!iIgnoreSpellIds->containsKey(pSpellId));
    }

    //===============================================
    // just return the instance
    IVMapManager* VMapFactory::createOrGetVMapManager()
    {
        if (gVMapManager == 0)
            gVMapManager= new VMapManager2();                // should be taken from config ... Please change if you like :-)
        return gVMapManager;
    }

    //===============================================
    // delete all internal data structures
    void VMapFactory::clear()
    {
        delete iIgnoreSpellIds;
        iIgnoreSpellIds = NULL;

        delete gVMapManager;
        gVMapManager = NULL;
    }
}
