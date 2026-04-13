#pragma once
#include <LibSWBF2/Types/LibString.h>
#include <unordered_map>
#include <string_view>

namespace LibSWBF2
{
	class CRC
	{
	private:
		static uint32_t m_Table32[256];
		static uint8_t m_ToLower[256];

	public:
		LIBSWBF2_API static CRCChecksum CalcLowerCRC(const char* str);
	};

	class FNV
	{
	public:
		LIBSWBF2_API static FNVHash Hash(const Types::String& str);
		LIBSWBF2_API static bool Lookup(FNVHash hash, Types::String& result);
		LIBSWBF2_API static void Register(const char* name);

		static constexpr FNVHash HashConstexpr(const char* str, const std::size_t length);

	private:
		static std::unordered_map<FNVHash, std::string>* p_LookupTable;
		static std::unordered_map<FNVHash, std::string>& GetTable();

#ifdef LOOKUP_CSV_PATH
	public:
		static void ReadLookupTable();
		static void ReleaseLookupTable();
#endif // LOOKUP_CSV_PATH
	};


	constexpr FNVHash FNV::HashConstexpr(const char* str, const std::size_t length)
	{
		constexpr uint32_t FNV_prime = 16777619;
		constexpr uint32_t offset_basis = 2166136261;

		uint32_t hash = offset_basis;

		for (std::size_t i = 0; i < length; i++)
		{
			char c = str[i];
			c |= 0x20;

			hash ^= c;
			hash *= FNV_prime;
		}

		return hash;
	}

	constexpr FNVHash operator""_fnv(const char* str, const std::size_t length)
	{
		return FNV::HashConstexpr(str, length);
	}
}
