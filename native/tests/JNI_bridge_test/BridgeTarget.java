public final class BridgeTarget
{
	public static final class Request
	{
		public String method;
		public String path;
		public String query;
		public byte[] body;
	}

	public static final class Response
	{
		public int status;
		public String contentType;
		public byte[] body;
	}

	public static Response fieldDispatch(Request r)
	{
		Response out = new Response();
		out.status = 200;
		out.contentType = "text/plain";
		out.body = r.body;
		return out;
	}

	public static int regionDispatch(byte[] in, byte[] out, int n)
	{
		int m = Math.min(n, out.length);
		System.arraycopy(in, 0, out, 0, m);
		return m;
	}

	public static int directDispatch(java.nio.ByteBuffer in, java.nio.ByteBuffer out, int n)
	{
		int m = Math.min(n, Math.min(in.remaining(), out.remaining()));
		for (int i = 0; i < m; i++)
		{
			out.put(i, in.get(i));
		}
		return m;
	}

	public static int directNoCopy(java.nio.ByteBuffer in, java.nio.ByteBuffer out, int n)
	{
		return Math.min(n, Math.min(in.remaining(), out.remaining()));
	}
}
