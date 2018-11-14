
#include "stdafx.h"
#include "Download.h"
#include "Bits.h"

void CBitsCtx::SelectChanges(ULONG ulChgOn, ULONG ulChgOff, BOOL fSignal)
{
    WORD wNewChg = 0;

    EnterCriticalSection();

    _wChgReq &= (WORD)~ulChgOff;

    if(fSignal
        && !(_wChgReq&DWNCHG_COMPLETE)
        && (ulChgOn&DWNCHG_COMPLETE)
        && GetDwnInfo()->TstFlags(DWNLOAD_COMPLETE))
    {
        wNewChg = DWNCHG_COMPLETE;
    }

    _wChgReq |= (WORD)ulChgOn;

    if(wNewChg)
    {
        super::Signal(wNewChg);
    }

    LeaveCriticalSection();
}

HRESULT CBitsCtx::GetStream(IStream** ppStream)
{
    return (((CBitsInfo*)GetDwnInfo())->GetStream(ppStream));
}


HRESULT CBitsInfo::Init(DWNLOADINFO* pdli)
{
    _dwClass = pdli->dwProgClass ? pdli->dwProgClass : PROGSINK_CLASS_MULTIMEDIA;

    RRETURN(super::Init(pdli));
}

CBitsInfo::~CBitsInfo()
{
    if(_hLock)
    {
        InternetUnlockRequestFile(_hLock);
    }

    if(_pDwnStm)
        _pDwnStm->Release();

    if(_fIsTemp && _cstrFile)
    {
        DeleteFile(_cstrFile);
    }
}

HRESULT CBitsInfo::GetFile(LPTSTR * ppch)
{
    RRETURN(_cstrFile ? MemAllocString(_cstrFile, ppch) : E_FAIL);
}

HRESULT CBitsInfo::NewDwnCtx(CDwnCtx** ppDwnCtx)
{
    *ppDwnCtx = new CBitsCtx;
    RRETURN(*ppDwnCtx ? S_OK : E_OUTOFMEMORY);
}

HRESULT CBitsInfo::NewDwnLoad(CDwnLoad** ppDwnLoad)
{
    *ppDwnLoad = new CBitsLoad;
    RRETURN(*ppDwnLoad ? S_OK : E_OUTOFMEMORY);
}

HRESULT CBitsInfo::OnLoadFile(LPCTSTR pszFile, HANDLE* phLock, BOOL fIsTemp)
{
    HRESULT hr = _cstrFile.Set(pszFile);

    if(hr == S_OK)
    {
        if(phLock)
        {
            _hLock = *phLock;
            *phLock = NULL;
        }

        _fIsTemp = fIsTemp;
    }

    RRETURN(hr);
}

void CBitsInfo::OnLoadDwnStm(CDwnStm* pDwnStm)
{
    Assert(_pDwnStm == NULL);
    _pDwnStm = pDwnStm;
    _pDwnStm->AddRef();
}

void CBitsInfo::OnLoadDone(HRESULT hrErr)
{
    Assert(EnteredCriticalSection());

    UpdFlags(DWNLOAD_MASK, !hrErr?DWNLOAD_COMPLETE:DWNLOAD_ERROR);
    Signal(DWNCHG_COMPLETE);
}

BOOL CBitsInfo::AttachEarly(UINT dt, DWORD dwRefresh, DWORD dwFlags, DWORD dwBindf)
{
    // In order to attach to an existing CBitsInfo, the following must match:
    //      _cstrUrl            (Already checked by caller)
    //      _dwRefresh
    //      DWNF_DOWNLOADONLY
    //      BINDF_OFFLINEOPERATION
    return (GetRefresh()==dwRefresh
        && GetFlags(DWNF_DOWNLOADONLY)==(dwFlags&DWNF_DOWNLOADONLY)
        && _dt==dt
        && (GetBindf()&BINDF_OFFLINEOPERATION)==(dwBindf&BINDF_OFFLINEOPERATION));
}

HRESULT CBitsInfo::GetStream(IStream** ppStream)
{
    HRESULT hr;

    if(_cstrFile)
    {
        hr = CreateStreamOnFile(_cstrFile,
            STGM_READ|STGM_SHARE_DENY_NONE, ppStream);
        if(hr)
        {
            goto Cleanup;
        }
    }
    else if(_pDwnStm)
    {
        hr = CreateStreamOnDwnStm(_pDwnStm, ppStream);
        if(hr)
        {
            goto Cleanup;
        }
    }
    else
    {
        hr = E_FAIL;
        *ppStream = NULL;
    }

Cleanup:
    RRETURN(hr);
}


CBitsLoad::~CBitsLoad()
{
    if(_pDwnStm)
    {
        _pDwnStm->Release();
    }

    ReleaseInterface(_pStmFile);
}

HRESULT CBitsLoad::Init(DWNLOADINFO* pdli, CDwnInfo* pDwnInfo)
{

    HRESULT hr;

    hr = super::Init(pdli, pDwnInfo, 
        0/*IDS_BINDSTATUS_DOWNLOADINGDATA_BITS wlw note*/,
        DWNF_GETFILELOCK|DWNF_NOAUTOBUFFER|DWNF_GETSTATUSCODE);

    RRETURN(hr);
}

HRESULT CBitsLoad::OnBindHeaders()
{
    LPCTSTR pch;
    HANDLE  hLock = NULL;
    HRESULT hr = S_OK;
    DWORD   dwStatusCode = _pDwnBindData->GetStatusCode();
    BOOL    fPretransform;

    if((dwStatusCode>=400) && (dwStatusCode<600))
    {
        hr = E_FAIL;
        goto Cleanup;
    }

    pch = _pDwnBindData->GetFileLock(&hLock, &fPretransform);

    if(fPretransform)
    {
        // we don't want to hold onto this file, we always want the bits
        if(hLock)
        {
            InternetUnlockRequestFile(hLock);
        }
        pch = NULL;
    }

    if(pch)
    {
        hr = GetBitsInfo()->OnLoadFile(pch, &hLock, FALSE);
        if(hr)
        {
            goto Cleanup;
        }

        _fGotFile = TRUE;

        if(_pDwnBindData->GetScheme()==URL_SCHEME_FILE
            || _pDwnBindData->IsFullyAvail())
        {
            CDwnDoc* pDwnDoc = _pDwnBindData->GetDwnDoc();

            if(pDwnDoc)
            {
                DWNPROG DwnProg;
                _pDwnBindData->GetProgress(&DwnProg);
                pDwnDoc->AddBytesRead(DwnProg.dwMax);
            }

            _pDwnBindData->Disconnect();
            OnDone(S_OK);

            hr = S_FALSE;
        }
    }

Cleanup:
    if(hLock)
    {
        InternetUnlockRequestFile(hLock);
    }
    RRETURN1(hr, S_FALSE);
}

HRESULT CBitsLoad::OnBindData()
{
    BYTE    ab[1024];
    ULONG   cb;
    HRESULT hr;

    if(!_fGotData)
    {
        _fGotData = TRUE;

        if(!_fGotFile)
        {
            if(GetBitsInfo()->_dt == DWNCTX_FILE)
            {
                if(_pDwnBindData->GetScheme() == URL_SCHEME_HTTPS)
                {
                    // No can do for secure connections.  The user has told
                    // us to not write secure data to disk (because otherwise
                    // we would have gotten a cache file name already).
                    hr = E_FAIL;
                    goto Cleanup;
                }

                // Create a temporary file for storing the data.

                TCHAR achFileName[MAX_PATH];
                TCHAR achPathName[MAX_PATH];
                DWORD dwRet;

                dwRet = GetTempPath(ARRAYSIZE(achPathName), achPathName);
                if(!(dwRet && dwRet<ARRAYSIZE(achPathName)))
                {
                    hr = E_FAIL;
                    goto Cleanup;
                }

                if(!GetTempFileName(achPathName, _T("dat"), 0, achFileName))
                {
                    hr = E_FAIL;
                    goto Cleanup;
                }

                hr = CreateStreamOnFile(achFileName,
                    STGM_READWRITE|STGM_SHARE_DENY_WRITE|STGM_CREATE,
                    &_pStmFile);
                if(hr)
                {
                    goto Cleanup;
                }

                hr = GetBitsInfo()->OnLoadFile(achFileName, NULL, TRUE);
                if(hr)
                {
                    goto Cleanup;
                }
            }
            else
            {
                // We only need to provide access to the data, not a file,
                // so just buffer the data as it comes in.
                _pDwnStm = new CDwnStm;

                if(_pDwnStm == NULL)
                {
                    hr = E_OUTOFMEMORY;
                    goto Cleanup;
                }

                _pDwnStm->SetSeekable();

                GetBitsInfo()->OnLoadDwnStm(_pDwnStm);
            }
        }
    }

    if(_pDwnStm)
    {
        void* pv;
        ULONG cbW, cbR;

        for(;;)
        {
            hr = _pDwnStm->WriteBeg(&pv, &cbW);
            if(hr)
            {
                goto Cleanup;
            }

            Assert(cbW > 0);

            hr = _pDwnBindData->Read(pv, cbW, &cbR);
            if(hr)
            {
                break;
            }

            Assert(cbR <= cbW);

            _pDwnStm->WriteEnd(cbR);

            if(cbR == 0)
            {
                break;
            }
        }
    }
    else
    {
        for(;;)
        {
            hr = _pDwnBindData->Read(ab, sizeof(ab), &cb);

            if(hr || !cb)
            {
                break;
            }

            if(_pStmFile)
            {
                hr = _pStmFile->Write(ab, cb, NULL);
                if(hr)
                {
                    goto Cleanup;
                }
            }
        }
    }

Cleanup:
    RRETURN(hr);
}