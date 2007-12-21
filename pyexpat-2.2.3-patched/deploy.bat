rem A script for copying the .py files to the emulator tree.
rem Assumes that the EPOCROOT environment variable has been set
rem to point to the correct tree. If not, just copy manually.

pushd %~dp0
mkdir %epocroot%epoc32\release\wins\udeb\z\system\libs
mkdir %epocroot%epoc32\release\wins\udeb\z\system\libs\xml
mkdir %epocroot%epoc32\release\wins\udeb\z\system\libs\xml\dom
mkdir %epocroot%epoc32\release\wins\udeb\z\system\libs\xml\parsers
mkdir %epocroot%epoc32\release\wins\udeb\z\system\libs\xml\sax
copy python-lib\xml\__init__.py %epocroot%epoc32\release\wins\udeb\z\system\libs\xml
copy python-lib\xml\dom\__init__.py %epocroot%epoc32\release\wins\udeb\z\system\libs\xml\dom
copy python-lib\xml\dom\domreg.py %epocroot%epoc32\release\wins\udeb\z\system\libs\xml\dom
copy python-lib\xml\dom\minidom.py %epocroot%epoc32\release\wins\udeb\z\system\libs\xml\dom
copy python-lib\xml\dom\pulldom.py %epocroot%epoc32\release\wins\udeb\z\system\libs\xml\dom
copy python-lib\xml\parsers\__init__.py %epocroot%epoc32\release\wins\udeb\z\system\libs\xml\parsers
copy python-lib\xml\parsers\expat.py %epocroot%epoc32\release\wins\udeb\z\system\libs\xml\parsers
copy python-lib\xml\sax\__init__.py %epocroot%epoc32\release\wins\udeb\z\system\libs\xml\sax
copy python-lib\xml\sax\_exceptions.py %epocroot%epoc32\release\wins\udeb\z\system\libs\xml\sax
copy python-lib\xml\sax\expatreader.py %epocroot%epoc32\release\wins\udeb\z\system\libs\xml\sax
copy python-lib\xml\sax\handler.py %epocroot%epoc32\release\wins\udeb\z\system\libs\xml\sax
copy python-lib\xml\sax\saxutils.py %epocroot%epoc32\release\wins\udeb\z\system\libs\xml\sax
copy python-lib\xml\sax\xmlreader.py %epocroot%epoc32\release\wins\udeb\z\system\libs\xml\sax
popd
