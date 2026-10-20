template<T> class Box {
    T* data;
    int32 n;
    [operator("create")] public void Init(int32 cap) { this->data = (T*)raw_alloc(cap * 8); this->n = 0; }
    [operator("delete")] public void Finalize() { raw_free(this->data); }
    public T Get(int32 i) { return this->data[i]; }
    public int32 Count() { return this->n; }
    public void Bump(int32 i);
}