#include "dictionary.hpp"
#include <bitset>

namespace utils
{

const char* Dictionary::DEFAULT_DICTIONARY_PATH = "z:/cross/bigdict.txt";
const char* Dictionary::DEFAULT_CONFIG_PATH = "z:/cross/config/config.ini";

Dictionary::Dictionary()
{
	try
	{
		boost::property_tree::ini_parser::read_ini(DEFAULT_CONFIG_PATH, _iniPropertyTree);
	}
	catch (const std::exception& e)
	{
		std::cout << "[ERROR]: Dictionary::Dictionary: Could not open default config path: " << DEFAULT_CONFIG_PATH << ". " << e.what() << std::endl;
		throw;
	}

	try 
	{
		_dictionaryFilePath = _iniPropertyTree.get<std::string>("dictionary.dictionary_file_path");
	}
	catch (const std::exception& e)
	{
		std::cout << "[ERROR]: Dictionary::Dictionary: Could not find dictionary.dictionary_file_path in config/config.ini. " << e.what() << std::endl;
		std::cout << "[INFO]: Dictionary::Dictionary: Defaulting to " << DEFAULT_DICTIONARY_PATH << std::endl;
		_dictionaryFilePath = DEFAULT_DICTIONARY_PATH;
	}

	loadDictionary();
	shuffle();
}

Dictionary::Dictionary(const std::string& configFilePath)
{
	try
	{
		boost::property_tree::ini_parser::read_ini(configFilePath, _iniPropertyTree);
	}
	catch (const std::exception& e)
	{
		std::cout << "[ERROR]: Dictionary::Dictionary: Could not open " << configFilePath << ". " << e.what() << std::endl;
		std::cout << "[INFO]: Dictionary::Dictionary: Defaulting to " << DEFAULT_CONFIG_PATH << std::endl;
		if (std::ifstream{ DEFAULT_CONFIG_PATH })
		{
			std::cout << "[ERROR]: Dictionary::Dictionary: Could not open default path: " << DEFAULT_CONFIG_PATH << "." << std::endl;
			throw std::runtime_error("Could not open the given configuration path nor the default configuration path");
		}
		boost::property_tree::ini_parser::read_ini(configFilePath, _iniPropertyTree);
	}

	try
	{
		_dictionaryFilePath = _iniPropertyTree.get<std::string>("dictionary.dictionary_file_path");
	}
	catch (const std::exception& e)
	{
		std::cout << "[ERROR]: Dictionary::Dictionary: Could not find dictionary.dictionary_file_path in " << configFilePath << ". " << e.what() << std::endl;
		std::cout << "[INFO]: Dictionary::Dictionary: Defaulting to " << DEFAULT_DICTIONARY_PATH << std::endl;
		_dictionaryFilePath = DEFAULT_DICTIONARY_PATH;
	}

	loadDictionary();
	shuffle();
}

Dictionary::~Dictionary()
{
	for (auto& it : _cacheSearchMap)
	{
		delete it.second;
	}
}

int Dictionary::levenstein(std::string a, std::string b)
{
	std::vector< std::vector<int> > dp(a.length() + 1, std::vector<int>(b.length() + 1, 0));

	for (uint32_t i = 0; i <= a.length(); ++i) 
	{
		for (uint32_t j = 0; j <= b.length(); ++j)
		{
			if (std::min(i, j) == 0)
				dp[i][j] = 0;
			else
			{
				dp[i][j] = std::min(dp[i - 1u][j] + 1, dp[i][j - 1u] + 1);
				dp[i][j] = std::min(dp[i][j], dp[i - 1u][j - 1u] + (a[i - 1u] != b[j - 1u]));
			}
		}
	}
	return dp[a.length()][b.length()];
}

std::string Dictionary::toupper(std::string capsWord)
{
	for (uint32_t i = 0; i < capsWord.size(); ++i)
		 capsWord[i] = toupper(capsWord[i]);

	return capsWord;
}
char Dictionary::toupper(char c)
{
	return uint8_t(c) >= CYRILLIC_A ? c-32 : c;
}

bool Dictionary::isalpha(char c)
{
	return uint8_t(c) >= CYRILLIC_A - 32;
}

/* Converts the dos code page to win 1251 cyrillic code page */
std::string Dictionary::dosToWinCode(std::string winWord)
{
	for (uint32_t i = 0; i < winWord.size(); ++i)
		if (uint8_t(winWord[i]) >= CYRILLIC_A - 96 && uint8_t(winWord[i]) < CYRILLIC_A - 32)
			winWord[i] += 64;

	return winWord;
}

std::string Dictionary::cleanString(const std::string& dirtyString)
{
	std::string clean; // Contains the clean word (only alphabetic symbols)

	for (uint32_t i = 0; i < dirtyString.size(); ++i)
		if (isalpha( dirtyString[i] ))
			clean.push_back(dirtyString[i]);

	return clean;
}

void Dictionary::reset()
{
	_allWords.clear();
	_dirtyDict.clear();
	_explanationDict.clear();
	_cacheSearchMap.clear();
	for (uint32_t i = 0; i < LONGEST_WORD; ++i)
		_fastSearch[i].clear();

}

void Dictionary::loadDictionary()
{
	reset();

	std::ifstream fin(_dictionaryFilePath);

	if (!fin.good())
	{
		std::cout << "[ERROR]: Dictionary::loadDictionary: Could not open file: " << _dictionaryFilePath << std::endl;
		return;
	}

	std::string nextWord;
	std::string explanation;

	while (getline(fin, nextWord, '\t')) // Get the next word
	{
		getline(fin, explanation); // Get the explanation for the word

		nextWord = dosToWinCode(nextWord);
		explanation = dosToWinCode(explanation);

		std::string clean( toupper(cleanString(nextWord)) );

		_dirtyDict.emplace(clean, nextWord);
		_explanationDict.emplace(clean, explanation);

		addToFastSearch(clean, _allWords.size());
		_allWords.push_back(clean);
	}

	std::cout << "[INFO]: Dictionary::loadDictionary: Loaded " << _allWords.size() << " words from dictionary" << std::endl;
}

void Dictionary::addToFastSearch(const std::string& newWord, uint16_t index)
{
	auto& currentFastSearch = _fastSearch[newWord.size()];
	
	for (uint32_t mask = 0u; mask < 64u; ++mask)
	{
		uint64_t key = getKey(mask, newWord);
		currentFastSearch[key].push_back(index);
	}
}

const std::string& Dictionary::getDirty(const std::string& clean) const 
{ 
	auto it = _dirtyDict.find(clean);
	if (it != _dirtyDict.end())
	{
		return it->second;
	}
	return "";
}
const std::string& Dictionary::getExplanation(const std::string& clean) const 
{ 
	auto it = _explanationDict.find(clean);
	if (it != _explanationDict.end())
	{
		return it->second;
	}
	return "";
}

void Dictionary::shuffle()
{
	for (auto length = 0; length < LONGEST_WORD; ++length)
	{
		for (auto& it : _fastSearch[length])
		{
			std::random_shuffle(it.second.begin(), it.second.end());
		}
	}

	for (auto& it : _cacheSearchMap)
	{
		std::random_shuffle(it.second->begin(), it.second->end());
	}
}

/* Efficient function to get key from word used in fastSearch*/
uint64_t Dictionary::getKey(uint32_t mask, const std::string& word)
{
	uint64_t key = 0ull;

	switch (word.size())
	{
	default:
	case 6:
		key |= ((mask >> 5 & 1u) ? (uint64_t(uint8_t(word[5])) << 40) : 0ull);
	case 5:
		key |= ((mask >> 4 & 1u) ? (uint64_t(uint8_t(word[4])) << 32) : 0ull);
	case 4:
		key |= ((mask >> 3 & 1u) ? (uint64_t(uint8_t(word[3])) << 24) : 0ull);
	case 3:
		key |= ((mask >> 2 & 1u) ? (uint64_t(uint8_t(word[2])) << 16) : 0ull);
	case 2:
		key |= ((mask >> 1 & 1u) ? (uint64_t(uint8_t(word[1])) << 8) : 0ull);
	case 1:
		key |= ((mask >> 0 & 1u) ? (uint64_t(uint8_t(word[0])) << 0) : 0ull);
	case 0:
		break;
	}
	return key;
}

/* Efficient function to get key from word used in fastSearch*/
uint64_t Dictionary::getKey(const std::string& word)
{
	uint64_t key = 0ull;

	switch (word.size())
	{
	default:
	case 6:
		key |= (uint64_t(uint8_t(word[5])) << 40);
	case 5:
		key |= (uint64_t(uint8_t(word[4])) << 32);
	case 4:
		key |= (uint64_t(uint8_t(word[3])) << 24);
	case 3:
		key |= (uint64_t(uint8_t(word[2])) << 16);
	case 2:
		key |= (uint64_t(uint8_t(word[1])) << 8);
	case 1:
		key |= (uint64_t(uint8_t(word[0])) << 0);
	case 0:
		break;
	}
	return key;
}

/* Returns whether the contender word matches the pattern*/
inline bool Dictionary::isPossible(const std::string& pattern, const std::string& contender, const std::vector<uint32_t>& filledIndices)
{
	for (uint32_t i : filledIndices)
		if (pattern[i] != contender[i])
			return false;
	
	return true;
}

/* Returns all words which satisfy this pattern */
Dictionary::Pattern Dictionary::findPossible(const std::string& pattern)
{
	{
		auto it = _cacheSearchMap.find(pattern);
		if (it != _cacheSearchMap.end())
		{
			auto& newCachePosition = it->second;

			return Pattern(newCachePosition->size(),
						   newCachePosition->begin(),
						   [this](uint16_t index) -> const std::string& { return getFromIndex(index); });
		}
	}

	auto key = getKey(pattern);
	auto& narrowedWords = _fastSearch[pattern.size()][key];
	
	if (pattern.size() <= 6) // The narrowedWords are the possible words
	{
		auto newCachePosition = new std::vector<uint16_t>(narrowedWords);
		_cacheSearchMap[pattern] = newCachePosition;

		return Pattern(newCachePosition->size(), 
					   newCachePosition->begin(), 
					   [this](uint16_t index) -> const std::string& { return getFromIndex(index); });
	}
	else
	{
		std::vector<uint16_t>* possibleWordIndices = new std::vector<uint16_t>;
		possibleWordIndices->reserve(narrowedWords.size());
		std::vector<uint32_t> filledIndices;
		filledIndices.reserve(pattern.size());

		for (uint32_t i = 0; i < pattern.size(); ++i)
			if (pattern[i] != ANY_CHAR)
				filledIndices.push_back(i);

		for (auto contender : narrowedWords)
			if (isPossible(pattern, getFromIndex(contender), filledIndices))
				possibleWordIndices->push_back(contender);

		_cacheSearchMap[pattern] = possibleWordIndices;

		return Pattern(possibleWordIndices->size(),
			           possibleWordIndices->begin(),
					   [this](uint16_t index) -> const std::string& { return getFromIndex(index); });
	}
}

}