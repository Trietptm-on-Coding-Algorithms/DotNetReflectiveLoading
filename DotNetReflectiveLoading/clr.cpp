#include "clr.hpp"
#include "utils.hpp"

namespace clr {
    ClrAssembly::ClrAssembly(mscorlib::_AssemblyPtr p) : p_(p)
    {
    }

    bool ClrAssembly::construct(const std::wstring & classname, variant_t & var)
    {
        HRESULT             hr = S_OK;
        bool                found = false;
        mscorlib::_TypePtr  pClsType = nullptr;
        mscorlib::_TypePtr* pTypes = nullptr;
        BSTR                pName = L"";
        SAFEARRAY*          pArray = nullptr;
        long                lower_bound = 0;
        long                upper_bound = 0;

        if (FAILED((hr = p_->GetTypes(&pArray)))) {
            LOG_ERROR("Failed to get types!", hr);
            return false;
        }
        SafeArrayGetLBound(pArray, 1, &lower_bound);
        SafeArrayGetUBound(pArray, 1, &upper_bound);
        SafeArrayAccessData(pArray, (void**)&pTypes);
        auto elem_count = upper_bound - lower_bound + 1;
        for (auto i = 0; i < elem_count; ++i) {
            pClsType = pTypes[i];
            if (FAILED((hr = pClsType->get_FullName(&pName)))) {
                LOG_ERROR("Failed to query for name!", hr);
                break;
            }

            if (pName == classname) {
                found = true;
                break;
            }
        }
        SafeArrayUnaccessData(pArray);
        if (!found)
            return false;
        if (FAILED((hr = p_->CreateInstance(pName, &var)))) {
            LOG_ERROR("Failed to create class instance!", hr);
            found = false;
        }

        return found;
    }

    ClrDomain::ClrDomain() : ClrDomain(clr_default_version)
    {
    }

    ClrDomain::ClrDomain(const std::wstring & clr_version)
    {
        HRESULT hr = S_OK;
        BOOL loadable = FALSE;
        LOG("Runtime initialization started...");

        if (FAILED((hr = CLRCreateInstance(CLSID_CLRMetaHost, IID_PPV_ARGS(pMeta_.GetAddressOf()))))) {
           LOG_ERROR("Failed to initialize metahost!", hr);
           throw EXCEPT("Host initialization failed!");
        }

        if (FAILED((hr = pMeta_->GetRuntime(clr_version.c_str(), IID_PPV_ARGS(pRuntime_.GetAddressOf()))))) {
           LOG_ERROR("Runtime initialization failed!", hr);
           throw EXCEPT("Runtime init failed!");
        }

        if (FAILED((hr = pRuntime_->IsLoadable(&loadable)) || !loadable)) {
           LOG_ERROR("Runtime not loadable!", hr);
           throw EXCEPT("Runtime not loadable!");
        }

        if (FAILED((hr = pRuntime_->GetInterface(CLSID_CorRuntimeHost, IID_PPV_ARGS(pHost_.GetAddressOf()))))) {
           LOG_ERROR("Failed to get runtime host!", hr);
           throw EXCEPT("Unable to host application!");
        }

        if (FAILED((hr = pHost_->Start()))) {
           LOG_ERROR("Host failed to start!", hr);
           throw EXCEPT("Host start failed!");
        }

        LOG("Initialization Complete!");
    }

    ClrDomain::~ClrDomain()
    {
        pHost_->Stop();
    }

    std::unique_ptr<ClrAssembly> ClrDomain::load(std::vector<uint8_t>& mod)
    {
        std::unique_ptr<ClrAssembly> clr;
        IUnknownPtr		        pDomainThunk = nullptr;
        mscorlib::_AppDomainPtr	pAppDomain = nullptr;
        mscorlib::_AssemblyPtr	pAsm = nullptr;
        HRESULT			        hr = S_OK;
        SAFEARRAY*		        pModContainer = nullptr;

        auto modSize = mod.size();
        if (modSize > ULONG_MAX) {
            LOG("Failed to load module, file size is too large!");
            return nullptr;
        }

        if (FAILED((hr = pHost_->GetDefaultDomain(&pDomainThunk)))) {
            LOG_ERROR("Failed to get default appdomain!", hr);
            return nullptr;
        }

        if (FAILED((hr = pDomainThunk->QueryInterface(IID_PPV_ARGS(&pAppDomain))))) {
            LOG_ERROR("Failed to get app domain interface from thunk!", hr);
            return nullptr;
        }

        if (nullptr == (pModContainer = SafeArrayCreateVector(VT_UI1, 0, static_cast<ULONG>(modSize)))) {
            LOG("Failed to allocate safe array vector!");
            return nullptr;
        }

        unsigned char* buf = nullptr;
        if (FAILED((hr = SafeArrayAccessData(pModContainer, reinterpret_cast<void**>(&buf))))) {
            LOG_ERROR("Failed to access safe array!", hr);
            return nullptr;
        }

        memcpy(buf, mod.data(), mod.size());
        SafeArrayUnaccessData(pModContainer);

        if (FAILED((hr = pAppDomain->Load_3(pModContainer, &pAsm)))) {
            LOG_ERROR("Failed to load assembly!", hr);
            return nullptr;
        }


        arr_.push_back(std::shared_ptr<SAFEARRAY>(pModContainer, [](auto p) { if (p) SafeArrayDestroy(p); }));
        clr = std::make_unique<ClrAssembly>(pAsm);

        return clr;
    }
}