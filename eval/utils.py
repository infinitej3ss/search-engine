from urllib.parse import urlparse

def normalize_url(url: str) -> str:
    """
    normalize url for comparison.
    we can change it more later but for now we strip www and trailing slashes
    """
    parsed = urlparse(url)
    domain = parsed.netloc.replace('www.', '')
    path = parsed.path.rstrip('/')
    return f"{domain}{path}"

