use std::{fs, io, path::Path, fs::File, io::BufReader};
use xz2::read::XzDecoder;
use tar::Archive;
use clap::Parser;
use indicatif::{ProgressBar, ProgressStyle};
use std::borrow::Cow;

/// Аргументы командной строки
#[derive(Parser)]
struct Args {
    /// Путь к архиву
    #[arg(short, long)]
    input: String,

    /// Папка для распаковки
    #[arg(short, long)]
    output: String,
}

/// Функция, которая форматирует название пакета до 20 символов:
/// - Если длиннее, обрезает и добавляет "...".
/// - Если короче, добавляет пробелы справа для выравнивания.
fn format_name(name: &str, max_len: usize) -> String {
    let name_len = name.chars().count();
    if name_len > max_len {
        let truncated: String = name.chars().take(max_len - 3).collect();
        format!("{}...", truncated)
    } else {
        format!("{:<max_len$}", name, max_len = max_len) // Дополняем пробелами
    }
}

fn main() -> io::Result<()> {
    // Разбираем аргументы
    let args = Args::parse();
    let archive_path = Path::new(&args.input);
    let output_dir = Path::new(&args.output);

    // Проверяем, существует ли архив
    if !archive_path.exists() {
        eprintln!("Ошибка: Файл {:?} не найден!", archive_path);
        return Ok(());
    }

    // Получаем имя архива без расширения
    let file_name = archive_path.file_name().unwrap().to_string_lossy();
    let file_stem: Cow<str> = file_name
        .strip_suffix(".tar.xz")
        .or_else(|| file_name.strip_suffix(".tar.gz"))
        .or_else(|| file_name.strip_suffix(".apg"))
        .unwrap_or(&file_name)
        .into();

    // Проверяем, поддерживается ли формат
    if !file_name.ends_with(".tar.xz") && !file_name.ends_with(".tar.gz") && !file_name.ends_with(".apg") {
        eprintln!("Ошибка: Формат '{}' не поддерживается!", file_name);
        return Ok(());
    }

    // Папка, куда будет распакован архив
    let extract_path = output_dir.join(&*file_stem);

    // Если папка уже существует, не распаковываем повторно
    if extract_path.exists() {
        println!("\rПакет '{}' уже распакован в {:?}", file_stem, extract_path);
        return Ok(());
    }

    // Создаём папку
    fs::create_dir_all(&extract_path)?;

    // Открываем файл и создаём декомпрессор
    let file = File::open(archive_path)?;
    let buf_reader = BufReader::new(file);
    let decompressor = XzDecoder::new(buf_reader);
    let mut archive = Archive::new(decompressor);

    // Настраиваем прогресс-бар
    let pb = ProgressBar::new(100);
    pb.set_style(ProgressStyle::default_bar()
        .template("\rUnpacking {prefix} [{bar:40}] {pos}%")
        .unwrap()
        .progress_chars("= "));

    // Форматируем название пакета (обрезаем или дополняем пробелами)
    let prefix = format_name(&file_stem, 20);
    pb.set_prefix(prefix);

    // Распаковываем файлы с прогрессом
    let mut entries = archive.entries()?;
    let mut count = 0;
    let total_files = entries.size_hint().1.unwrap_or(100);

    while let Some(entry) = entries.next() {
        let mut entry = entry?;
        entry.unpack_in(&extract_path)?;
        count += 1;

        let percent = (count * 100 / total_files).min(100);
        pb.set_position(percent as u64);
    }

    pb.finish_with_message(format!("\rThe package '{}' has been successfully converted", file_stem));

    Ok(())
}
