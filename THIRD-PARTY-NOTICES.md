# 외부 구성 요소와 라이선스 고지

인터벌의 직접 작성한 소스, 스크립트, 문서는 MIT 라이선스로 제공됩니다. 아래 폰트, Microsoft 구성 요소, 외부 API와 데이터에는 각각의 권리자와 제공자의 조건이 적용됩니다. 이 파일은 원본 라이선스를 대체하지 않습니다.

## 1. 샌드박스 어그로체

- 사용 서체: 샌드박스 어그로체 Light / Medium
- 파일: `SB_Aggro_L.ttf`, `SB_Aggro_M.ttf`
- 권리자: (주)샌드박스네트워크
- 저작권: © SANDBOX NETWORK. All Rights Reserved.
- 라이선스: 샌드박스 자체 라이선스. MIT / SIL OFL 아님
- 사용 형태: 수정하지 않은 원본 TTF를 Windows 실행 파일의 리소스로 임베딩
- 공식 배포: <https://www.sandbox.co.kr/company-ci>
- 원본 ZIP: <https://www.sandbox.co.kr/uploads/sbx-content/ci/SBX_AGGRO_FONT_2026.zip>
- 폰트 소개: <https://noonnu.cc/font_page/738>

소스 저장소의 라이선스 원문은 `assets/SB_Aggro_Font_license.pdf`입니다. 실행 파일 배포 ZIP에는 같은 원문을 `SB_Aggro_Font_license.pdf`로 동봉합니다.

원본은 개인·기업의 영리·비영리 사용과 프로그램 임베딩을 허용하는 항목을 포함합니다. OFL 항목은 미허용으로 표시되어 있으므로, 폰트 파일을 MIT 또는 OFL 대상이나 독립 재배포가 허용된 파일로 취급하지 않습니다. TTF 파일은 Git에서 제외하며 `fetch-fonts.ps1`이 공식 배포처에서 내려받습니다. 서체를 다른 용도로 사용하거나 따로 배포하려면 원본 조건을 확인해야 합니다.

원본 라이선스에는 어그로체를 사용한 작업물을 샌드박스의 마케팅 활동과 자료 수집에 활용할 수 있다는 안내도 있습니다. 이를 원하지 않을 때의 문의 주소는 `aggro-font@sandboxnetwork.net`입니다.

## 2. Microsoft Windows / Visual C++

인터벌은 Windows에 포함된 Win32, GDI/GDI+, WinHTTP, Shell, 세션·전원 관련 API를 사용합니다. Windows 시스템 DLL이나 Windows SDK 자체를 프로젝트 ZIP에 별도로 동봉하지 않습니다.

릴리스 빌드는 MSVC의 `/MT` 옵션으로 Microsoft C/C++ 런타임을 실행 파일에 정적으로 링크합니다. 이 Microsoft 코드와 개발 도구에는 해당 Microsoft 소프트웨어 라이선스가 적용됩니다. 인터벌의 MIT 라이선스가 Microsoft 코드의 별도 권리나 재배포 조건을 변경하지 않습니다.

관련 공식 안내:

- Visual C++ 파일 재배포: <https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files?view=msvc-170>
- Microsoft C++ 배포와 정적 링크: <https://learn.microsoft.com/en-us/cpp/windows/deployment-in-visual-cpp?view=msvc-170>
- Visual Studio 라이선스: <https://visualstudio.microsoft.com/license-terms/>
- Visual Studio 2022 재배포 목록: <https://aka.ms/vs/17/redist.txt>

앱을 빌드하고 재배포하는 개발자는 자신이 사용하는 Visual Studio / Build Tools / SDK의 라이선스와 재배포 조건을 따라야 합니다. 정적으로 링크한 런타임의 업데이트를 실행 파일에 반영하려면 다시 빌드해야 합니다.

### Microsoft C++ 표준 라이브러리(STL)

빌드에 사용한 MSVC 14.44.35207의 표준 라이브러리 헤더에는 Microsoft 저작권과 `Apache-2.0 WITH LLVM-exception` 식별자가 표시되어 있습니다. `std::vector`, `std::string`, `std::thread` 등은 이 구현을 사용합니다.

- 저작권: Copyright (c) Microsoft Corporation.
- 라이선스: Apache License 2.0 with LLVM Exception
- 공식 원문: <https://github.com/microsoft/STL/blob/main/LICENSE.txt>
- 소스 저장소의 원문 사본: [MSVC-STL-LICENSE.txt](assets/licenses/MSVC-STL-LICENSE.txt)
- 실행 파일 배포 ZIP의 원문 사본: `MSVC-STL-LICENSE.txt`

LLVM 예외는 소스 컴파일 과정에서 실행 파일에 포함되는 라이브러리 부분의 배포에 관한 조건을 명시합니다. 이 표준 라이브러리를 인터벌의 MIT 적용 대상에 포함하지 않으며, 원문을 함께 제공합니다.

## 3. 공개 시세 API와 데이터

다음 공개 REST API에서 비트코인 가격과 캔들 데이터를 받습니다. 거래소의 SDK 소스나 로고를 앱에 포함하지 않으며, 계정 인증이나 API 키를 사용하지 않습니다.

| 제공자 | 사용 API 문서 |
| --- | --- |
| Binance | [공개 REST 안내](https://developers.binance.com/en/docs/products/spot/rest-api), [시세 엔드포인트](https://developers.binance.com/en/docs/catalog/core-trading-spot-trading/api/rest-api/market) |
| OKX | [API 문서](https://www.okx.com/docs-v5/en/) |
| 빗썸 | [현재가](https://apidocs.bithumb.com/reference/현재가-조회), [분 캔들](https://apidocs.bithumb.com/reference/분minute-캔들-조회) |
| 업비트 | [현재가](https://docs.upbit.com/kr/reference/list-tickers), [Open API 이용약관](https://upbit.com/service_center/open_api_terms) |

API 접근, 요청 제한, 데이터 이용·재배포에는 각 제공자의 이용 조건이 적용됩니다. 이 프로젝트의 MIT 라이선스는 거래소 API나 시세 데이터의 이용권을 부여하지 않습니다. 가격·차트 화면에는 데이터 출처를 표시합니다. 거래소 명칭은 출처 설명을 위해 사용하며, 거래소가 앱을 보증하거나 제휴한다는 뜻이 아닙니다.

## 4. 그 밖의 라이브러리와 이미지

현재 별도의 외부 오픈소스 GUI·차트·JSON 라이브러리를 포함하지 않습니다. JSON 파서와 UI 코드는 프로젝트 소스에 직접 작성되어 있습니다. README의 스크린샷은 실제 인터벌 앱을 캡처한 이미지이며, 화면에 표시된 폰트와 시세에는 위의 조건이 적용됩니다.
