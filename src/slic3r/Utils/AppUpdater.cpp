#include "AppUpdater.hpp"

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <curl/curl.h>

#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/format.hpp"
#include "slic3r/Utils/Http.hpp"

namespace Slic3r {

namespace {
	size_t write_data(void* ptr, size_t size, size_t nmemb, FILE* stream) {
		size_t written = fwrite(ptr, size, nmemb, stream);
		return written;
	}

	int get_file(const std::string& url, const boost::filesystem::path& target_path) {
		CURL* curl;
		FILE* fp;
		CURLcode res;
		char outfilename[FILENAME_MAX] = "mydownload2.exe";
		curl = curl_easy_init();
		if (curl) {
			fp = fopen(outfilename, "wb");
			curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
			res = curl_easy_perform(curl);
			/* always cleanup */
			curl_easy_cleanup(curl);
			fclose(fp);
		}
		return 0;
	}


	// downloads file into std::string
	bool get_text_file(const std::string& url, const boost::filesystem::path& target_path)
	{
		bool res = false;
		boost::filesystem::path tmp_path = target_path;
		tmp_path += format(".%1%.download", get_current_pid());

		BOOST_LOG_TRIVIAL(info) << GUI::format("Get: `%1%`\n\t-> `%2%`\n\tvia tmp path `%3%`",
			url,
			target_path.string(),
			tmp_path.string());

		Http::get(url)
			.on_progress([](Http::Progress, bool& cancel) {
			if (cancel) { cancel = true; }
				})
			.on_error([&](std::string body, std::string error, unsigned http_status) {
				(void)body;
				BOOST_LOG_TRIVIAL(error) << GUI::format("Error getting: `%1%`: HTTP %2%, %3%",
					url,
					http_status,
					error);
			})
			.on_complete([&](std::string body, unsigned /* http_status */) {
				boost::filesystem::fstream file(tmp_path, std::ios::out | std::ios::binary | std::ios::trunc);
				file.write(body.c_str(), body.size());
				file.close();
				boost::filesystem::rename(tmp_path, target_path);
				res = true;
			})
			.perform_sync();
			return res;
	}
}

void AppUpdater::download_file(const DownloadAppData& data)
{
	bool res = get_file(data.url, data.target);
	BOOST_LOG_TRIVIAL(error) << "Download from " << data.url << " to " << data.target.string() << " was " << res;
}

} //namespace Slic3r 