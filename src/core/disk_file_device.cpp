#include "core/disk_file_device.h"

#include "core/file_system.h"
#include "core/ifile.h"
#include "core/ifile_system_defines.h"
#include "core/os_file.h"


namespace Lux
{
	namespace FS
	{
		class DiskFile : public IFile
		{
		public:
			DiskFile() {}
			virtual ~DiskFile() {}

			virtual bool open(const char* path, Mode mode) LUX_OVERRIDE
			{
				return m_file.open(path, mode);
			}

			virtual void close() LUX_OVERRIDE
			{
				m_file.close();
			}

			virtual bool read(void* buffer, intptr_t size) LUX_OVERRIDE
			{
				return m_file.read(buffer, size);
			}

			virtual bool write(const void* buffer, intptr_t size) LUX_OVERRIDE
			{
				return m_file.write(buffer, size);
			}

			virtual const void* getBuffer() const LUX_OVERRIDE
			{
				return NULL;
			}

			virtual intptr_t size() LUX_OVERRIDE
			{
				return m_file.size();
			}

			virtual intptr_t seek(SeekMode base, intptr_t pos) LUX_OVERRIDE
			{
				return m_file.seek(base, pos);
			}

			virtual intptr_t pos() LUX_OVERRIDE
			{
				return m_file.pos();
			}

		private:
			OsFile m_file;
		};

		IFile* DiskFileDevice::createFile(IFile* child)
		{
			return LUX_NEW(DiskFile)();
		}
	} // namespace FS
} // ~namespace Lux