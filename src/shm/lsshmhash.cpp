/*****************************************************************************
*    Open LiteSpeed is an open source HTTP server.                           *
*    Copyright (C) 2013 - 2015  LiteSpeed Technologies, Inc.                 *
*                                                                            *
*    This program is free software: you can redistribute it and/or modify    *
*    it under the terms of the GNU General Public License as published by    *
*    the Free Software Foundation, either version 3 of the License, or       *
*    (at your option) any later version.                                     *
*                                                                            *
*    This program is distributed in the hope that it will be useful,         *
*    but WITHOUT ANY WARRANTY; without even the implied warranty of          *
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the            *
*    GNU General Public License for more details.                            *
*                                                                            *
*    You should have received a copy of the GNU General Public License       *
*    along with this program. If not, see http://www.gnu.org/licenses/.      *
*****************************************************************************/
#include <shm/lsshmhash.h>

#include <lsr/xxhash.h>
#include <shm/lsshmpool.h>

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>



const uint8_t LsShmHash::s_bitMask[] =
{
    0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80
};
const size_t LsShmHash::s_bitsPerChar =
  sizeof(LsShmHash::s_bitMask)/sizeof(LsShmHash::s_bitMask[0]);


enum { prime_count = 31    };
static const size_t s_primeList[prime_count] =
{
    7ul,          13ul,         29ul,
    53ul,         97ul,         193ul,       389ul,       769ul,
    1543ul,       3079ul,       6151ul,      12289ul,     24593ul,
    49157ul,      98317ul,      196613ul,    393241ul,    786433ul,
    1572869ul,    3145739ul,    6291469ul,   12582917ul,  25165843ul,
    50331653ul,   100663319ul,  201326611ul, 402653189ul, 805306457ul,
    1610612741ul, 3221225473ul, 4294967291ul
};


static int findRange(size_t sz)
{
    int i = 1;
    for (; i < prime_count - 1; ++i)
    {
        if (sz <= s_primeList[i])
            break;
    }
    return i;
}


static size_t roundUp(size_t sz)
{
    return s_primeList[findRange(sz)];
}


// Hash for 32bytes session id
LsShmHKey LsShmHash::hash32id(const void *__s, int len)
{
    const uint32_t *lp = (const uint32_t *)__s;
    LsShmHKey __h = 0;

    if (len >= 8)
        __h = *lp ^ *(lp + 1);
    else
    {
        while (len >= 4)
        {
            __h ^= *lp++;
            len -= 4;
        }
        // ignore the leftover!
        // if (len)
    }
    return __h;
}


LsShmHKey LsShmHash::hashBuf(const void *__s, int len)
{
    LsShmHKey __h = 0;
    const uint8_t *p = (const uint8_t *)__s;
    uint8_t ch;

    // we will need a better hash key generator for buf key
    while (--len >= 0)
    {
        ch = *(const uint8_t *)p++;
        __h = __h * 31 + (ch);
    }
    return __h;
}

LsShmHKey LsShmHash::hashXXH32(const void *__s, int len)
{
    return XXH32(__s, len, 0);
}


int  LsShmHash::compBuf(const void *pVal1, const void *pVal2, int len)
{
    return memcmp((const char *)pVal1, (const char *)pVal2, len);
}


LsShmHKey LsShmHash::hashString(const void *__s, int len)
{
    LsShmHKey __h = 0;
    const char *p = (const char *)__s;
    char ch = *(const char *)p++;
    for (; ch ; ch = *((const char *)p++))
        __h = __h * 31 + (ch);

    return __h;
}


int LsShmHash::compString(const void *pVal1, const void *pVal2, int len)
{
    return strcmp((const char *)pVal1, (const char *)pVal2);
}


LsShmHKey LsShmHash::iHashString(const void *__s, int len)
{
    LsShmHKey __h = 0;
    const char *p = (const char *)__s;
    char ch = *(const char *)p++;
    for (; ch ; ch = *((const char *)p++))
    {
        if (ch >= 'A' && ch <= 'Z')
            ch += 'a' - 'A';
        __h = __h * 31 + (ch);
    }
    return __h;
}


int LsShmHash::iCompString(const void *pVal1, const void *pVal2, int len)
{
    return strncasecmp(
               (const char *)pVal1, (const char *)pVal2, strlen((const char *)pVal1));
}


LsShmHKey LsShmHash::hfIpv6(const void *pKey, int len)
{
    LsShmHKey key;
    if (sizeof(LsShmHKey) == 4)
    {
        key = *((const LsShmHKey *)pKey) +
              *(((const LsShmHKey *)pKey) + 1) +
              *(((const LsShmHKey *)pKey) + 2) +
              *(((const LsShmHKey *)pKey) + 3);
    }
    else
    {
        key = *((const LsShmHKey *)pKey) +
              *(((const LsShmHKey *)pKey) + 1);
    }
    return key;
}


int LsShmHash::cmpIpv6(const void *pVal1, const void *pVal2, int len)
{
    return memcmp(pVal1, pVal2, 16);
}


void LsShmHash::remap()
{
    m_pPool->checkRemap();
    if ((m_pShmMap != m_pPool->getShmMap())
        || (m_iCapacity != m_pTable->x_iCapacity))
    {
        m_pTable = (LsShmHTable *)m_pPool->offset2ptr(m_iOffset);

#if 0
        SHM_NOTICE(
            "LsShmHash::remap %6d %X %X SIZE %X %X size %d cap %d [%d]",
            getpid(), m_pShmMap,
            m_pPool->getShmMap(),
            m_pPool->getShmMapMaxSize(),
            m_pPool->getShmMapOldMaxSize(),
            m_pTable->x_iSize,
            m_pTable->x_iCapacity,
            m_iCapacity
        );
#endif
        m_pIdxStart = (LsShmHIdx *)m_pPool->offset2ptr(m_pTable->x_iTable);
        m_pIdxEnd = m_pIdxStart + m_pTable->x_iCapacity;
        m_pBitMap = (uint8_t *)m_pPool->offset2ptr(m_pTable->x_iBitMap);
        if (m_pTable->x_iLruOffset != 0)
        {
            m_pLru =
                (LsHashLruInfo *)m_pPool->offset2ptr(m_pTable->x_iLruOffset);
        }
        m_pShmMap = m_pPool->getShmMap();
        m_iCapacity = m_pTable->x_iCapacity;
        m_pShmLock = m_pPool->lockPool()->offset2pLock(m_pTable->x_iLockOffset);
        m_pShmGLock =
            m_pPool->lockPool()->offset2pLock(m_pTable->x_iGLockOffset);
    }
    else
    {
#ifdef DEBUG_RUN
        SHM_NOTICE(
            "LsShmHash::remapXXX NOCHANGE %6d %X %X SIZE %X %X size %d cap %d",
            getpid(), m_pShmMap,
            m_pPool->getShmMap(),
            m_pPool->getShmMapMaxSize(),
            m_pPool->getShmMapOldMaxSize(),
            m_pTable->x_iSize,
            m_pTable->x_iCapacity
        );
#endif
    }
}


LsShmHash::LsShmHash(LsShmPool *pool, const char *name, size_t init_size,
                     LsShmHash::hash_fn hf, LsShmHash::val_comp vc)
    : m_iMagic(LSSHM_HASH_MAGIC)
    , m_pTable(NULL)
    , m_pIdxStart(NULL)
    , m_pIdxEnd(NULL)
    , m_pBitMap(NULL)
    , m_pLru(NULL)

    , m_pPool(pool)
    , m_iOffset(0)
    , m_pName(strdup(name))

    , m_hf(hf)
    , m_vc(vc)
{
    m_iRef = 0;
    m_iCapacity = 0;
    m_status = LSSHM_NOTREADY;
    m_pShmMap = NULL;
    m_pShmLock = NULL;
    m_iLockEnable = 1;

    if ((m_pName == NULL) || (strlen(m_pName) >= LSSHM_MAXNAMELEN))
        return;

    if (m_hf != NULL)
    {
        assert(m_vc);
        m_insert = insertPtr;
        m_update = updatePtr;
        m_set = setPtr;
        m_find = findPtr;
        m_get = getPtr;
        m_iMode = 1;
    }
    else
    {
        m_insert = insertNum;
        m_update = updateNum;
        m_set = setNum;
        m_find = findNum;
        m_get = getNum;
        m_iMode = 0;
    }
    init_size = roundUp(init_size);

    LsShmReg *p_reg = m_pPool->findReg(name);
    if (p_reg == NULL)
    {
        if ((p_reg = m_pPool->addReg(name)) == NULL)
        {
            m_status = LSSHM_BADMAPFILE;
            return;
        }
    }
    m_iOffset = p_reg->x_iValue;
    if (m_iOffset == 0)
    {
        LsShmOffset_t regOffset = m_pPool->ptr2offset(p_reg);

        // Create new HASH Table
        int remapped = 0;

        // NOTE: system is not up yet... ignore remap here
        m_iOffset = m_pPool->alloc2(sizeof(LsShmHTable), remapped);
        if (m_iOffset == 0)
            return;
        LsShmOffset_t iTable = m_pPool->alloc2(sz2TableSz(init_size), remapped);
        LsShmOffset_t iBitMap = m_pPool->alloc2(sz2BitMapSz(init_size), remapped);

        // MAP in the system here...
        m_pShmMap = m_pPool->getShmMap();
        m_pTable = (LsShmHTable *)m_pPool->offset2ptr(m_iOffset);

        m_pTable->x_iMagic = LSSHM_HASH_MAGIC;
        m_pTable->x_iCapacity = init_size;
        m_pTable->x_iSize = 0;
        m_pTable->x_iFullFactor = 2;
        m_pTable->x_iGrowFactor = 2;
        m_pTable->x_iTable = iTable;
        m_pTable->x_iBitMap = iBitMap;
        m_pTable->x_iLockOffset = 0;
        m_pTable->x_iLruOffset = 0;

        if ((iTable == 0) || (iBitMap == 0))
        {
            initErrCleanUp();
            return;
        }

        // setup local and global lock only if new
        m_pShmLock = m_pPool->lockPool()->allocLock();
        m_pShmGLock = m_pPool->lockPool()->allocLock();
        if ((m_pShmLock == NULL) || (m_pShmGLock == NULL))
        {
            initErrCleanUp();
            return;
        }
        if ((setupLock() != 0) || (setupGLock() != 0))
        {
            initErrCleanUp();
            return;
        }
        m_pTable->x_iLockOffset = m_pPool->lockPool()->pLock2offset(m_pShmLock);
        m_pTable->x_iGLockOffset =
            m_pPool->lockPool()->pLock2offset(m_pShmGLock);

        m_pIdxStart = (LsShmHIdx *)m_pPool->offset2ptr(m_pTable->x_iTable);
        m_pIdxEnd = m_pIdxStart + m_pTable->x_iCapacity;
        m_pBitMap = (uint8_t *)m_pPool->offset2ptr(m_pTable->x_iBitMap);
        m_iCapacity = m_pTable->x_iCapacity;

        // clear all idx
        ::memset(m_pIdxStart, 0, sz2TableSz(m_pTable->x_iCapacity));
        ::memset(m_pBitMap, 0, sz2BitMapSz(m_pTable->x_iCapacity));

        LsShmReg *xp_reg = (LsShmReg *)m_pPool->offset2ptr(regOffset);

        xp_reg->x_iValue = m_iOffset;
        m_pTable->x_iMode = m_iMode;
        m_pTable->x_iLruMode = 0;
    }
    else
    {
        remap();

        // check the magic and mode
        if ((m_iMagic != m_pTable->x_iMagic) || (m_iMode != m_pTable->x_iMode))
            return;
    }

    m_iRef = 1;
    m_status = LSSHM_READY;
#ifdef DEBUG_RUN
    SHM_NOTICE("LsShmHash::LsShmHash insert %s <%p>", m_pName, &m_objBase);
#endif
    m_pPool->getShm()->getObjBase().insert(m_pName, this);
}


LsShmHash::~LsShmHash()
{
    if (m_pName != NULL)
    {
        if (m_iRef == 0)
        {
#ifdef DEBUG_RUN
            SHM_NOTICE("LsShmHash::~LsShmHash remove %s <%p>",
                            m_pName, &m_objBase);
#endif
            m_pPool->getShm()->getObjBase().remove(m_pName);
        }
        free(m_pName);
        m_pName = NULL;
    }
}


LsShmHash *LsShmHash::checkHTable(GHash::iterator itor, LsShmPool *pool,
                                  const char *name, LsShmHash::hash_fn hf, LsShmHash::val_comp vc)
{
    LsShmHash *pObj;
    if (((pObj = (LsShmHash *)itor->second()) == NULL)
        || (pObj->m_iMagic != LSSHM_HASH_MAGIC)
        || (pObj->m_hf != hf)
        || (pObj->m_vc != vc))
        return NULL;    // bad: parameters not matching

    if (pObj->m_pPool != pool)
        return (LsShmHash *) - 1; // special case: different pools
    pObj->upRef();
    return pObj;
}


void LsShmHash::setFullFactor(int f)   
{   
    if (f > 0)
    {
        getHTable()->x_iFullFactor = f;  
    }
}


void LsShmHash::setGrowFactor(int f)   
{   
    if (f > 0) 
    {
        getHTable()->x_iGrowFactor = f;  
    }
}


void LsShmHash::initErrCleanUp()
{
    size_t size = m_pTable->x_iCapacity;
    if (m_pTable->x_iTable != 0)
    {
        m_pPool->release2(m_pTable->x_iTable, sz2TableSz(size));
        m_pTable->x_iTable = 0;
    }
    if (m_pTable->x_iBitMap != 0)
    {
        m_pPool->release2(m_pTable->x_iBitMap, sz2BitMapSz(size));
        m_pTable->x_iBitMap = 0;
    }
    if (m_iOffset != 0)
    {
        m_pPool->release2(m_iOffset, sizeof(LsShmHTable));
        m_iOffset = 0;
    }
    if (m_pShmLock != NULL)
    {
        m_pPool->lockPool()->freeLock(m_pShmLock);
        m_pShmLock = NULL;
    }
    if (m_pShmGLock != NULL)
    {
        m_pPool->lockPool()->freeLock(m_pShmGLock);
        m_pShmGLock = NULL;
    }
}


void LsShmHash::close()
{
//     LsShmPool *p = NULL;
//     if (m_iPoolOwner != 0)
//     {
//         m_iPoolOwner = 0;
//         p = m_pPool;
//     }
    if (downRef() == 0)
        delete this;
//     if (p != NULL)
//         p->close();
}


//
//  The only way to remove the Shared Memory
//
void LsShmHash::destroy()
{
    if (m_iOffset != 0)
    {
        // all elements
        clear();
        if (m_pTable->x_iTable != 0)
        {
            m_pPool->release2(
                m_pTable->x_iTable, sz2TableSz(m_pTable->x_iCapacity));
        }
        if (m_pTable->x_iBitMap != 0)
        {
            m_pPool->release2(
                m_pTable->x_iBitMap, sz2BitMapSz(m_pTable->x_iCapacity));
        }
        if (m_pTable->x_iLruOffset != 0)
            m_pPool->release2(m_pTable->x_iLruOffset, sizeof(LsHashLruInfo));
        m_pPool->release2(m_iOffset, sizeof(LsShmHTable));

        // remove from regMap
        LsShmReg *p_reg = m_pPool->findReg(m_pName);
        p_reg->x_iValue = 0;
        m_iOffset = 0;
        m_pTable = NULL;
        m_pIdxStart = NULL;
        m_pIdxEnd = NULL;
        m_pBitMap = NULL;
        m_pLru = NULL;
    }
}


int LsShmHash::rehash()
{
    checkRemap();
    int range = findRange(m_pTable->x_iCapacity);
    int newSize = s_primeList[range + m_pTable->x_iGrowFactor];

#ifdef DEBUG_RUN
    SHM_NOTICE("LsShmHash::rehash %6d %X %X size %d cap %d NEW %d",
                    getpid(), m_pShmMap, m_pPool->getShmMap(),
                    m_pTable->x_iSize,
                    m_pTable->x_iCapacity,
                    newSize
                   );
#endif
    int remapped = 0;
    LsShmOffset_t newIdxOff = m_pPool->alloc2(sz2TableSz(newSize), remapped);
    if (newIdxOff == 0)
        return LS_FAIL;
    LsShmOffset_t newBitOff = m_pPool->alloc2(sz2BitMapSz(newSize), remapped);
    if (newBitOff == 0)
    {
        m_pPool->release2(newIdxOff, sz2TableSz(newSize));
        return LS_FAIL;
    }

    // if (remapped)
    remap();
    LsShmHIdx *pIdx = (LsShmHIdx *)m_pPool->offset2ptr(newIdxOff);
    ::memset(pIdx, 0, sz2TableSz(newSize));

    // change bitmap now, so setBitMapEnt may be called in loop.
    m_pPool->release2(
        m_pTable->x_iBitMap, sz2BitMapSz(m_pTable->x_iCapacity));
    m_pTable->x_iBitMap = newBitOff;
    m_pBitMap = (uint8_t *)m_pPool->offset2ptr(newBitOff);
    ::memset(m_pBitMap, 0, sz2BitMapSz(newSize));

    iterator iter;
    iteroffset iterOff;
    for (iterOff = begin(); iterOff != end(); iterOff = next(iterOff))
    {
        iter = offset2iterator(iterOff);
        uint32_t hashIndx = getIndex(iter->x_hkey, newSize);
        LsShmHIdx *npIdx = pIdx + hashIndx;
        setBitMapEnt(hashIndx);
        assert(npIdx->x_iOffset < m_pPool->getShmMap()->x_iMaxSize);
        iter->x_iNext = npIdx->x_iOffset;
        npIdx->x_iOffset = iterOff;
    }

    m_pPool->release2(
        m_pTable->x_iTable, sz2TableSz(m_pTable->x_iCapacity));
    m_pTable->x_iTable = newIdxOff;
    m_pTable->x_iCapacity = newSize;
    m_iCapacity = newSize;
    m_pIdxStart = pIdx;
    m_pIdxEnd = m_pIdxStart + newSize;
    remap();
    return 0;
}


int LsShmHash::release_hash_elem(LsShmHash::iteroffset iterOff, void *pUData)
{
    LsShmHash *pThis = (LsShmHash *)pUData;
    LsShmHash::iterator iter = pThis->offset2iterator(iterOff);
    pThis->release2(iterOff, iter->x_iLen);
    return 0;
}


void LsShmHash::clear()
{
    checkRemap();
    int n = for_each2(begin(), end(), release_hash_elem, this);
    assert(n == (int)m_pTable->x_iSize);

    ::memset(m_pIdxStart, 0, sz2TableSz(m_pTable->x_iCapacity));
    ::memset(m_pBitMap, 0, sz2BitMapSz(m_pTable->x_iCapacity));
    m_pTable->x_iSize = 0;
    if (m_pLru != NULL)
    {
        m_pLru->linkFirst = 0;
        m_pLru->linkLast = 0;
        m_pLru->nvalset = 0;
        m_pLru->ndataset = 0;
    }
}


//
// @brief erase - remove iter from the SHM pool.
// @brief will destroy the link to itself if any!
//
void LsShmHash::eraseIteratorHelper(iterator iter)
{
    if (iter == NULL)
        return;

    iteroffset iterOff = m_pPool->ptr2offset(iter);
    uint32_t hashIndx = getIndex(iter->x_hkey, m_pTable->x_iCapacity);
    LsShmHIdx *pIdx = m_pIdxStart + hashIndx;
    LsShmOffset_t offset = pIdx->x_iOffset;
    LsShmHElem *pElem;

#ifdef DEBUG_RUN
    if (offset == 0)
    {
        SHM_NOTICE(
            "LsShmHash::eraseIteratorHelper %6d %X %X size %d cap %d",
            getpid(), m_pShmMap, m_pPool->getShmMap(),
            m_pTable->x_iSize,
            m_pTable->x_iCapacity
        );
        sleep(10);
    }
#endif

    unlinkHElem(iter);
    if (offset == iterOff)
    {
        if ((pIdx->x_iOffset = iter->x_iNext) == 0) // last one
            clrBitMapEnt(hashIndx);
    }
    else
    {
        do
        {
            pElem = (LsShmHElem *)m_pPool->offset2ptr(offset);
            if (pElem->x_iNext == iterOff)
            {
                pElem->x_iNext = iter->x_iNext;
                break;
            }
            // next offset...
            offset = pElem->x_iNext;
        }
        while (offset != 0);
    }

    m_pPool->release2(iterOff, iter->x_iLen);
    --m_pTable->x_iSize;
}


LsShmHash::iteroffset LsShmHash::find2( LsShmHKey key, ls_str_pair_t *pParms)
{
    uint32_t hashIndx = getIndex(key, m_pTable->x_iCapacity);
    if (getBitMapEnt(hashIndx) == 0)     // quick check
        return 0;
    LsShmHIdx *pIdx = m_pIdxStart + hashIndx;

#ifdef DEBUG_RUN
    SHM_NOTICE("LsShmHash::find %6d %X %X size %d cap %d <%p> %d",
                    getpid(), m_pShmMap, m_pPool->getShmMap(),
                    m_pTable->x_iSize,
                    m_pTable->x_iCapacity,
                    pIdx,
                    getIndex(key, m_pTable->x_iCapacity)
                   );
#endif
    LsShmOffset_t offset = pIdx->x_iOffset;
    LsShmHElem *pElem;

    while (offset != 0)
    {
        pElem = (LsShmHElem *)m_pPool->offset2ptr(offset);
        if ((pElem->x_hkey == key)
            && (pElem->getKeyLen() == ls_str_len(&pParms->key))
            && ((*m_vc)(ls_str_buf(&pParms->key), pElem->getKey(),
                        ls_str_len(&pParms->key)) == 0))
            return offset;
        offset = pElem->x_iNext;
        remap();
    }
    return 0;
}


LsShmHash::iteroffset LsShmHash::insert2(LsShmHKey key, ls_str_pair_t *pParms)
{
    uint32_t valueOff = sizeof(ls_vardata_t) + round4(ls_str_len(&pParms->key));
    int valLen = ls_str_len(&pParms->value);
    valueSetup(&valueOff, &valLen);
    int elementSize = sizeof(LsShmHElem) + valueOff
      + sizeof(ls_vardata_t) + round4(valLen);
    int remapped = 0;
    LsShmOffset_t offset = m_pPool->alloc2(elementSize, remapped);
    if (offset == 0)
        return 0;

    remap();
    if (m_pTable->x_iSize * m_pTable->x_iFullFactor > m_pTable->x_iCapacity)
    {
        if (rehash() < 0)
        {
            if (m_pTable->x_iSize == m_pTable->x_iCapacity)
                return 0;
        }
    }
    LsShmHElem *pNew = (LsShmHElem *)m_pPool->offset2ptr(offset);

    pNew->x_iLen = elementSize;
    pNew->x_iValOff = valueOff;
    // pNew->x_iNext = 0;
    pNew->x_hkey = key;
    pNew->setKeyLen(ls_str_len(&pParms->key));
    pNew->setValLen(valLen);

    setIterKey(pNew, ls_str_buf(&pParms->key));
    setIterData(pNew, ls_str_buf(&pParms->value));
    linkHElem(pNew, offset);

    uint32_t hashIndx = getIndex(key, m_pTable->x_iCapacity);
    LsShmHIdx *pIdx = m_pIdxStart + hashIndx;
    pNew->x_iNext = pIdx->x_iOffset;
    pIdx->x_iOffset = offset;
    setBitMapEnt(hashIndx);

#ifdef DEBUG_RUN
    SHM_NOTICE("LsShmHash::insert %6d %X %X size %d cap %d <%p> %d",
                    getpid(), m_pShmMap, m_pPool->getShmMap(),
                    m_pTable->x_iSize,
                    m_pTable->x_iCapacity,
                    pIdx,
                    getIndex(key, m_pTable->x_iCapacity)
                   );
#endif
    ++m_pTable->x_iSize;

    return offset;
}


LsShmHash::iteroffset LsShmHash::findNum(LsShmHash *pThis,
                                       ls_str_pair_t *pParms)
{
    LsShmHKey key = (LsShmHKey)(long)ls_str_buf(&pParms->key);
    uint32_t hashIndx = pThis->getIndex(key, pThis->m_pTable->x_iCapacity);
    if (pThis->getBitMapEnt(hashIndx) == 0)     // quick check
        return 0;
    LsShmHIdx *pIdx = pThis->m_pIdxStart + hashIndx;
    LsShmOffset_t offset = pIdx->x_iOffset;
    LsShmHElem *pElem;

    while (offset != 0)
    {
        pElem = (LsShmHElem *)pThis->m_pPool->offset2ptr(offset);
        // check to see if the key is the same
        if ((pElem->x_hkey == key) && ((*(LsShmHKey *)pElem->getKey()) == key))
            return offset;
        offset = pElem->x_iNext;
    }
    return 0;
}


LsShmHash::iteroffset LsShmHash::getNum(LsShmHash *pThis,
                                        ls_str_pair_t *pParms, int *pFlag)
{
    iteroffset iterOff = findNum(pThis, pParms);
    char *keyptr = ls_str_buf(&pParms->key);
    ls_str_pair_t nparms;
    ls_str_unsafeset(&nparms.key, (char *)&keyptr, sizeof(LsShmHKey));
    nparms.value = pParms->value;

    return doGet(pThis, iterOff, (LsShmHKey)(long)keyptr, &nparms, pFlag);
}


LsShmHash::iteroffset LsShmHash::insertNum(LsShmHash *pThis,
                                           ls_str_pair_t *pParms)
{
    iteroffset iterOff = findNum(pThis, pParms);
    char *keyptr = ls_str_buf(&pParms->key);
    ls_str_pair_t nparms;
    ls_str_unsafeset(&nparms.key, (char *)&keyptr, sizeof(LsShmHKey));
    nparms.value = pParms->value;

    return doInsert(pThis, iterOff, (LsShmHKey)(long)keyptr, &nparms);
}


LsShmHash::iteroffset LsShmHash::setNum(LsShmHash *pThis,
                                        ls_str_pair_t *pParms)
{
    iteroffset iterOff = findNum(pThis, pParms);
    char *keyptr = ls_str_buf(&pParms->key);
    ls_str_pair_t nparms;
    ls_str_unsafeset(&nparms.key, (char *)&keyptr, sizeof(LsShmHKey));
    nparms.value = pParms->value;

    return doSet(pThis, iterOff, (LsShmHKey)(long)keyptr, &nparms);
}


LsShmHash::iteroffset LsShmHash::updateNum(LsShmHash *pThis,
                                           ls_str_pair_t *pParms)
{
    iteroffset iterOff = findNum(pThis, pParms);
    char *keyptr = ls_str_buf(&pParms->key);
    ls_str_pair_t nparms;
    ls_str_unsafeset(&nparms.key, (char *)&keyptr, sizeof(LsShmHKey));
    nparms.value = pParms->value;

    return doUpdate(pThis, iterOff, (LsShmHKey)(long)keyptr, &nparms);
}


LsShmHash::iteroffset LsShmHash::findPtr(LsShmHash *pThis,
                                         ls_str_pair_t *pParms)
{
    return pThis->find2((*pThis->m_hf)(
        ls_str_buf(&pParms->key), ls_str_len(&pParms->key)), pParms);
}


LsShmHash::iteroffset LsShmHash::getPtr(LsShmHash *pThis,
                                      ls_str_pair_t *pParms, int *pFlag)
{
    LsShmHKey key = (*pThis->m_hf)(
        ls_str_buf(&pParms->key), ls_str_len(&pParms->key));
    iteroffset iterOff = pThis->find2(key, pParms);

    return doGet(pThis, iterOff, key, pParms, pFlag);
}


LsShmHash::iteroffset LsShmHash::insertPtr(LsShmHash *pThis, ls_str_pair_t *pParms)
{
    LsShmHKey key = (*pThis->m_hf)(
        ls_str_buf(&pParms->key), ls_str_len(&pParms->key));
    iteroffset iterOff = pThis->find2(key, pParms);

    return doInsert(pThis, iterOff, key, pParms);
}


LsShmHash::iteroffset LsShmHash::setPtr(LsShmHash *pThis, ls_str_pair_t *pParms)
{
    LsShmHKey key = (*pThis->m_hf)(
        ls_str_buf(&pParms->key), ls_str_len(&pParms->key));
    iteroffset iterOff = pThis->find2(key, pParms);

    return doSet(pThis, iterOff, key, pParms);
}


LsShmHash::iteroffset LsShmHash::updatePtr(LsShmHash *pThis, ls_str_pair_t *pParms)
{
    LsShmHKey key = (*pThis->m_hf)(
        ls_str_buf(&pParms->key), ls_str_len(&pParms->key));
    iteroffset iterOff = pThis->find2(key, pParms);

    return doUpdate(pThis, iterOff, key, pParms);
}


LsShmHash::iteroffset LsShmHash::begin()
{
    checkRemap();
    if (m_pTable->x_iSize == 0)
        return end();

    LsShmHIdx *p = m_pIdxStart;
    while (p < m_pIdxEnd)
    {
        if (p->x_iOffset != 0)
            return (iteroffset)p->x_iOffset;
        ++p;
    }
    return end();
}


LsShmHash::iteroffset LsShmHash::next(iteroffset iterOff)
{
    if (iterOff == end())
        return end();
    iterator iter = offset2iterator(iterOff);
    if (iter->x_iNext != 0)
        return (iteroffset)iter->x_iNext;

    LsShmHIdx *p;
    checkRemap();
    p = m_pIdxStart + getIndex(iter->x_hkey, m_pTable->x_iCapacity) + 1;
    while (p < m_pIdxEnd)
    {
        if (p->x_iOffset != 0)
        {
#ifdef DEBUG_RUN
            iterator xiter = (iterator)m_pPool->offset2ptr(p->x_iOffset);
            if (xiter != NULL)
            {
                if ((xiter->getKeyLen() == 0)
                    || (xiter->x_hkey == 0)
                    || (xiter->x_iLen == 0))
                {
                    SHM_NOTICE(
                        "LsShmHash::next PROBLEM %6d %X %X SIZE %X SLEEPING",
                        getpid(), m_pShmMap,
                        m_pPool->getShmMap(),
                        m_pPool->getShmMapMaxSize());
                    sleep(10);
                }
            }
#endif
            return (iteroffset)p->x_iOffset;
        }
        ++p;
    }
    return end();
}


int LsShmHash::for_each(iteroffset beg, iteroffset end, for_each_fn fun)
{
    if (fun == NULL)
    {
        errno = EINVAL;
        return LS_FAIL;
    }
    int n = 0;
    iteroffset iterNext = beg;
    iteroffset iterOff;
    while ((iterNext != 0) && (iterNext != end))
    {
        iterOff = iterNext;
        iterNext = next(iterNext);      // get next before fun
        if (fun(iterOff) != 0)
            break;
        ++n;
    }
    return n;
}


int LsShmHash::for_each2(
    iteroffset beg, iteroffset end, for_each_fn2 fun, void *pUData)
{
    if (fun == NULL)
    {
        errno = EINVAL;
        return LS_FAIL;
    }
    int n = 0;
    iteroffset iterNext = beg;
    iteroffset iterOff;
    while ((iterNext != 0) && (iterNext != end))
    {
        iterOff = iterNext;
        iterNext = next(iterNext);      // get next before fun
        if (fun(iterOff, pUData) != 0)
            break;
        ++n;
    }
    return n;
}


//
//  @brief statIdx - helper function which return num of elements in this link
//
int LsShmHash::statIdx(iteroffset iterOff, for_each_fn2 fun, void *pUdata)
{
    LsHashStat *pHashStat = (LsHashStat *)pUdata;
#define NUM_SAMPLE  0x20
    typedef struct statKey_s
    {
        LsShmHKey    key;
        int          numDup;
    } statKey_t;
    statKey_t keyTable[NUM_SAMPLE];
    statKey_t *p_keyTable;
    int numKey = 0;
    int curDup = 0; // keep track the number of dup!

    int numInIdx = 0;
    while (iterOff != 0)
    {
        iterator iter = offset2iterator(iterOff);
        int i;
        p_keyTable = keyTable;
        for (i = 0; i < numKey; ++i, ++p_keyTable)
        {
            if (p_keyTable->key == iter->x_hkey)
            {
                ++p_keyTable->numDup;
                ++curDup;
                break;
            }
        }
        if ((i == numKey) && (i < NUM_SAMPLE))
        {
            p_keyTable->key = iter->x_hkey;
            p_keyTable->numDup = 0;
            ++numKey;
        }

        ++numInIdx;
        fun(iterOff, pUdata);
        iterOff = iter->x_iNext;
    }

    pHashStat->numDup += curDup;
    return numInIdx;
}


//
//  @brief stat - populate the statistic of the hash table
//  @brief return num of elements searched.
//  @brief populate the pHashStat.
//
int LsShmHash::stat(LsHashStat *pHashStat, for_each_fn2 fun)
{
    if (pHashStat == NULL)
    {
        errno = EINVAL;
        return LS_FAIL;
    }
    ::memset(pHashStat, 0, sizeof(LsHashStat));

    // search each idx
    LsShmHIdx *p = m_pIdxStart;
    while (p < m_pIdxEnd)
    {
        ++pHashStat->numIdx;
        if (p->x_iOffset != 0)
        {
            ++pHashStat->numIdxOccupied;
            int num;
            if ((num = statIdx((iteroffset)p->x_iOffset,
                               fun, (void *)pHashStat)) != 0)
            {
                pHashStat->num += num;
                if (num > pHashStat->maxLink)
                    pHashStat->maxLink = num;

                // top 10 listing
                int topidx;
                if (num <= 5)
                    topidx = num - 1;
                else if (num <= 10)
                    topidx = 5;
                else if (num <= 20)
                    topidx = 6;
                else if (num <= 50)
                    topidx = 7;
                else if (num <= 100)
                    topidx = 8;
                else
                    topidx = 9;
                ++pHashStat->top[topidx];
            }
        }
        ++p;
    }
    return pHashStat->num;
}

